#include <nlib/vector.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// Counts live instances to check that elements are constructed/destroyed once.
struct Counted {
  static int alive;
  int value;

  explicit Counted(int v) : value(v) { ++alive; }
  Counted(const Counted& other) : value(other.value) { ++alive; }
  Counted(Counted&& other) noexcept : value(other.value) { ++alive; }
  Counted& operator=(const Counted&) = default;
  Counted& operator=(Counted&&) = default;
  ~Counted() { --alive; }
};
int Counted::alive = 0;

// True if `v`'s elements are stored inside the object itself.
template <typename T>
bool uses_inline_storage(const nq::vector<T>& v) {
  const auto* p = reinterpret_cast<const std::byte*>(v.data());
  const auto* lo = reinterpret_cast<const std::byte*>(&v);
  return p >= lo && p < lo + sizeof(v);
}

TEST(Vector, DefaultConstructedIsEmptyAndInline) {
  nq::vector<int> v;
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0u);
  EXPECT_GE(v.capacity(), 1u);
  EXPECT_TRUE(uses_inline_storage(v));
}

TEST(Vector, SmallElementTypeFitsOneCacheLine) {
  EXPECT_EQ(sizeof(nq::vector<int>), 64u);
  EXPECT_EQ(sizeof(nq::vector<char>), 64u);
}

TEST(Vector, InlinePushesDoNotAllocate) {
  nq::vector<int> v;
  const int* initial = v.data();
  const auto inline_cap = v.capacity();
  for (std::size_t i = 0; i < inline_cap; ++i) v.push_back(static_cast<int>(i));
  EXPECT_EQ(v.data(), initial);
  EXPECT_TRUE(uses_inline_storage(v));
  for (std::size_t i = 0; i < inline_cap; ++i) EXPECT_EQ(v[i], static_cast<int>(i));
}

TEST(Vector, ReserveMovesToHeapAndKeepsValues) {
  nq::vector<int> v;
  for (int i = 0; i < 4; ++i) v.push_back(i);
  v.reserve(1000);
  EXPECT_FALSE(uses_inline_storage(v));
  EXPECT_GE(v.capacity(), 1000u);
  ASSERT_EQ(v.size(), 4u);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(v[i], i);

  for (int i = 4; i < 1000; ++i) v.push_back(i);
  for (int i = 0; i < 1000; ++i) ASSERT_EQ(v[static_cast<std::size_t>(i)], i);
}

TEST(Vector, CountAndValueConstructors) {
  nq::vector<int> zeros(5);
  EXPECT_EQ(zeros, (nq::vector<int>{0, 0, 0, 0, 0}));

  nq::vector<int> sevens(3, 7);
  EXPECT_EQ(sevens, (nq::vector<int>{7, 7, 7}));

  const std::vector<int> src{1, 2, 3, 4};
  nq::vector<int> from_range(src.begin(), src.end());
  EXPECT_EQ(from_range, (nq::vector<int>{1, 2, 3, 4}));
}

TEST(Vector, InsertAndEraseInTheMiddle) {
  nq::vector<int> v{1, 2, 5};
  auto it = v.insert(v.begin() + 2, 2, 9);
  EXPECT_EQ(*it, 9);
  EXPECT_EQ(v, (nq::vector<int>{1, 2, 9, 9, 5}));

  it = v.erase(v.begin() + 2, v.begin() + 4);
  EXPECT_EQ(*it, 5);
  EXPECT_EQ(v, (nq::vector<int>{1, 2, 5}));

  it = v.emplace(v.begin() + 2, 3);
  EXPECT_EQ(*it, 3);
  EXPECT_EQ(v, (nq::vector<int>{1, 2, 3, 5}));

  v.erase(v.begin());
  EXPECT_EQ(v, (nq::vector<int>{2, 3, 5}));
}

TEST(Vector, InsertGrowsAcrossInlineBoundary) {
  nq::vector<int> v;
  const auto inline_cap = v.capacity();
  for (std::size_t i = 0; i < inline_cap; ++i) v.push_back(1);

  v.insert(v.begin(), 3, 2);
  EXPECT_EQ(v.size(), inline_cap + 3);
  EXPECT_FALSE(uses_inline_storage(v));
  EXPECT_EQ(v[0], 2);
  EXPECT_EQ(v[3], 1);
}

TEST(Vector, InsertOwnElementSurvivesReallocation) {
  nq::vector<std::string> v;
  v.reserve(2);
  v.emplace_back("first-long-enough-to-defeat-sso");
  v.emplace_back("second");
  v.insert(v.begin(), 4, v[0]);  // reallocation invalidates the source reference
  ASSERT_EQ(v.size(), 6u);
  for (int i = 0; i < 5; ++i) EXPECT_EQ(v[static_cast<std::size_t>(i)], "first-long-enough-to-defeat-sso");
  EXPECT_EQ(v[5], "second");
}

TEST(Vector, ResizeGrowsAndShrinks) {
  nq::vector<int> v{1, 2, 3};
  v.resize(6);
  EXPECT_EQ(v, (nq::vector<int>{1, 2, 3, 0, 0, 0}));
  v.resize(8, 4);
  EXPECT_EQ(v, (nq::vector<int>{1, 2, 3, 0, 0, 0, 4, 4}));
  v.resize(2);
  EXPECT_EQ(v, (nq::vector<int>{1, 2}));
}

TEST(Vector, ShrinkToFitReturnsToInlineStorage) {
  nq::vector<int> v;
  v.reserve(100);
  for (int i = 0; i < 3; ++i) v.push_back(i);
  ASSERT_FALSE(uses_inline_storage(v));

  v.shrink_to_fit();
  EXPECT_TRUE(uses_inline_storage(v));
  EXPECT_EQ(v, (nq::vector<int>{0, 1, 2}));
}

TEST(Vector, ShrinkToFitTrimsHeapBuffer) {
  nq::vector<int> v;
  v.reserve(1000);
  for (int i = 0; i < 500; ++i) v.push_back(i);
  v.shrink_to_fit();
  EXPECT_EQ(v.capacity(), 500u);
  for (int i = 0; i < 500; ++i) ASSERT_EQ(v[static_cast<std::size_t>(i)], i);
}

TEST(Vector, CopyConstructAndAssign) {
  nq::vector<int> v{1, 2, 3};
  nq::vector<int> copy(v);
  EXPECT_EQ(copy, v);

  nq::vector<int> assigned{9, 9, 9, 9};
  assigned = v;
  EXPECT_EQ(assigned, v);
}

TEST(Vector, MoveStealsHeapBuffer) {
  nq::vector<int> v;
  v.reserve(100);
  for (int i = 0; i < 50; ++i) v.push_back(i);
  const int* buffer = v.data();

  nq::vector<int> moved(std::move(v));
  EXPECT_EQ(moved.data(), buffer);
  EXPECT_EQ(moved.size(), 50u);
  EXPECT_TRUE(v.empty());
  EXPECT_TRUE(uses_inline_storage(v));
}

TEST(Vector, MoveOfInlineVectorMovesElements) {
  nq::vector<std::string> v;
  v.push_back("a");  // one element always fits the inline buffer
  ASSERT_TRUE(uses_inline_storage(v));

  nq::vector<std::string> moved(std::move(v));
  EXPECT_TRUE(uses_inline_storage(moved));
  ASSERT_EQ(moved.size(), 1u);
  EXPECT_EQ(moved[0], "a");
  EXPECT_TRUE(v.empty());
}

TEST(Vector, SwapExchangesContents) {
  nq::vector<int> a{1, 2, 3};
  nq::vector<int> b{9};
  swap(a, b);
  EXPECT_EQ(a, (nq::vector<int>{9}));
  EXPECT_EQ(b, (nq::vector<int>{1, 2, 3}));

  a.swap(a);  // self-swap keeps contents
  EXPECT_EQ(a, (nq::vector<int>{9}));
}

TEST(Vector, MoveOnlyElements) {
  nq::vector<std::unique_ptr<int>> v;
  v.reserve(8);
  for (int i = 0; i < 8; ++i) v.push_back(std::make_unique<int>(i));
  v.reserve(100);  // relocation must move, not copy
  for (int i = 0; i < 8; ++i) EXPECT_EQ(*v[static_cast<std::size_t>(i)], i);

  nq::vector<std::unique_ptr<int>> moved(std::move(v));
  EXPECT_EQ(*moved.front(), 0);
  moved.pop_back();
  EXPECT_EQ(moved.size(), 7u);
}

TEST(Vector, ElementsAreDestroyedExactlyOnce) {
  ASSERT_EQ(Counted::alive, 0);
  {
    nq::vector<Counted> v;
    v.reserve(50);
    for (int i = 0; i < 50; ++i) v.emplace_back(i);
    EXPECT_EQ(Counted::alive, 50);

    v.pop_back();
    EXPECT_EQ(Counted::alive, 49);

    v.erase(v.begin(), v.begin() + 9);
    EXPECT_EQ(Counted::alive, 40);

    v.clear();
    EXPECT_EQ(Counted::alive, 0);

    v.emplace_back(1);
    EXPECT_EQ(Counted::alive, 1);
  }
  EXPECT_EQ(Counted::alive, 0);
}

TEST(Vector, IterationAndReverseIteration) {
  nq::vector<int> v{1, 2, 3, 4};
  int sum = 0;
  for (int x : v) sum += x;
  EXPECT_EQ(sum, 10);

  std::vector<int> reversed(v.rbegin(), v.rend());
  EXPECT_EQ(reversed, (std::vector<int>{4, 3, 2, 1}));
}

TEST(Vector, AssignReplacesContents) {
  nq::vector<int> v{1, 2, 3};
  v.assign(4, 7);
  EXPECT_EQ(v, (nq::vector<int>{7, 7, 7, 7}));

  v = {5, 6};
  EXPECT_EQ(v, (nq::vector<int>{5, 6}));
}

}  // namespace
