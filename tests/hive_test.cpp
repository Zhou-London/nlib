#include <nlib/hive.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Iteration order is unspecified, so contents are compared as a sorted copy.
template <typename T>
std::vector<T> sorted_elements(const nlib::hive<T>& h) {
  std::vector<T> out(h.begin(), h.end());
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<int> iota_vector(int n, int first = 0) {
  std::vector<int> v(static_cast<std::size_t>(n));
  std::iota(v.begin(), v.end(), first);
  return v;
}

// Counts live instances to check that elements are constructed/destroyed once.
struct Counted {
  static int alive;
  int value;

  explicit Counted(int v) : value(v) { ++alive; }
  ~Counted() { --alive; }
};
int Counted::alive = 0;

static_assert(!std::is_copy_constructible_v<nlib::hive<int>>);
static_assert(!std::is_copy_assignable_v<nlib::hive<int>>);

TEST(Hive, DefaultConstructedIsEmpty) {
  nlib::hive<int> h;
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(h.size(), 0u);
  EXPECT_EQ(h.begin(), h.end());
}

TEST(Hive, InsertGrowsSize) {
  nlib::hive<int> h;
  for (int i = 0; i < 100; ++i) h.emplace(i);
  EXPECT_FALSE(h.empty());
  EXPECT_EQ(h.size(), 100u);
  EXPECT_EQ(sorted_elements(h), iota_vector(100));
}

TEST(Hive, InsertMovesValue) {
  nlib::hive<std::unique_ptr<int>> h;
  auto p = std::make_unique<int>(7);
  auto it = h.insert(std::move(p));
  EXPECT_EQ(p, nullptr);
  EXPECT_EQ(**it, 7);
}

TEST(Hive, EmplaceReturnsIteratorToNewElement) {
  nlib::hive<std::pair<int, int>> h;
  auto it = h.emplace(1, 2);
  EXPECT_EQ(it->first, 1);
  EXPECT_EQ(it->second, 2);
  EXPECT_EQ(h.size(), 1u);
}

TEST(Hive, EraseRemovesOnlyTheTarget) {
  nlib::hive<int> h;
  std::vector<nlib::hive<int>::iterator> its;
  for (int i = 0; i < 20; ++i) its.push_back(h.emplace(i));

  h.erase(its[7]);
  EXPECT_EQ(h.size(), 19u);

  std::vector<int> expected = iota_vector(20);
  expected.erase(expected.begin() + 7);
  EXPECT_EQ(sorted_elements(h), expected);
}

TEST(Hive, EraseReturnsFollowingIterator) {
  nlib::hive<int> h;
  for (int i = 0; i < 30; ++i) h.emplace(i);

  auto it = h.begin();
  ++it;
  auto expected = it;
  ++expected;
  EXPECT_EQ(h.erase(it), expected);
  EXPECT_EQ(h.size(), 29u);
}

TEST(Hive, EraseEveryOtherElementKeepsIterationConsistent) {
  nlib::hive<int> h;
  for (int i = 0; i < 200; ++i) h.emplace(i);

  for (auto it = h.begin(); it != h.end();) {
    if (*it % 2 == 0)
      it = h.erase(it);
    else
      ++it;
  }

  EXPECT_EQ(h.size(), 100u);
  for (int v : h) EXPECT_EQ(v % 2, 1);
  EXPECT_EQ(std::distance(h.begin(), h.end()), 100);
}

TEST(Hive, EraseRangeClearsAll) {
  nlib::hive<int> h;
  for (int i = 0; i < 50; ++i) h.emplace(i);
  EXPECT_EQ(h.erase(h.begin(), h.end()), h.end());
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(h.begin(), h.end());
}

TEST(Hive, InsertReusesErasedSlots) {
  nlib::hive<int> h;
  for (int i = 0; i < 64; ++i) h.emplace(i);
  const auto capacity_before = h.capacity();

  for (auto it = h.begin(); it != h.end();) it = (*it % 2 == 0) ? h.erase(it) : std::next(it);
  for (int i = 0; i < 32; ++i) h.emplace(1000 + i);

  EXPECT_EQ(h.size(), 64u);
  EXPECT_EQ(h.capacity(), capacity_before);
}

TEST(Hive, ElementAddressesAreStableAcrossInserts) {
  nlib::hive<int> h;
  std::vector<int*> addresses;
  for (int i = 0; i < 100; ++i) addresses.push_back(&*h.emplace(i));
  for (int i = 0; i < 100; ++i) EXPECT_EQ(*addresses[static_cast<std::size_t>(i)], i);
}

TEST(Hive, ReverseIterationVisitsAllElements) {
  nlib::hive<int> h;
  for (int i = 0; i < 40; ++i) h.emplace(i);

  std::vector<int> reversed(h.rbegin(), h.rend());
  ASSERT_EQ(reversed.size(), 40u);
  std::vector<int> forward(h.begin(), h.end());
  std::reverse(forward.begin(), forward.end());
  EXPECT_EQ(reversed, forward);
}

TEST(Hive, ClearFreesStorage) {
  nlib::hive<int> h;
  for (int i = 0; i < 100; ++i) h.emplace(i);
  EXPECT_GT(h.capacity(), 0u);

  h.clear();
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(h.capacity(), 0u);

  h.insert(1);
  EXPECT_EQ(h.size(), 1u);
  EXPECT_EQ(sorted_elements(h), (std::vector<int>{1}));
}

TEST(Hive, MoveConstructAndAssign) {
  nlib::hive<int> h;
  for (int i = 0; i < 30; ++i) h.emplace(i);

  nlib::hive<int> moved(std::move(h));
  EXPECT_EQ(moved.size(), 30u);
  EXPECT_EQ(sorted_elements(moved), iota_vector(30));

  nlib::hive<int> assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.size(), 30u);
  EXPECT_EQ(sorted_elements(assigned), iota_vector(30));
}

TEST(Hive, SwapExchangesContents) {
  nlib::hive<int> a;
  for (int v : {1, 2, 3}) a.emplace(v);
  nlib::hive<int> b;
  b.emplace(9);

  swap(a, b);
  EXPECT_EQ(sorted_elements(a), (std::vector<int>{9}));
  EXPECT_EQ(sorted_elements(b), (std::vector<int>{1, 2, 3}));
}

TEST(Hive, GetIteratorRecoversElement) {
  nlib::hive<int> h;
  for (int i = 0; i < 20; ++i) h.emplace(i);

  auto it = h.begin();
  std::advance(it, 5);
  EXPECT_EQ(h.get_iterator(&*it), it);

  const int outside = 0;
  EXPECT_EQ(h.get_iterator(&outside), h.end());
}

TEST(Hive, GetIteratorReturnsEndForErasedSlot) {
  nlib::hive<int> h;
  auto it = h.insert(42);
  const int* p = &*it;
  h.erase(it);
  EXPECT_EQ(h.get_iterator(p), h.end());
}

TEST(Hive, ElementsAreDestroyedExactlyOnce) {
  ASSERT_EQ(Counted::alive, 0);
  {
    nlib::hive<Counted> h;
    for (int i = 0; i < 50; ++i) h.emplace(i);
    EXPECT_EQ(Counted::alive, 50);

    h.erase(h.begin());
    EXPECT_EQ(Counted::alive, 49);

    h.clear();
    EXPECT_EQ(Counted::alive, 0);

    for (int i = 0; i < 10; ++i) h.emplace(i);
    EXPECT_EQ(Counted::alive, 10);
  }
  EXPECT_EQ(Counted::alive, 0);
}

TEST(Hive, ConstIterationCoversAllElements) {
  nlib::hive<int> h;
  for (int i = 0; i < 25; ++i) h.emplace(i);

  const auto& ch = h;
  EXPECT_EQ(std::distance(ch.cbegin(), ch.cend()), 25);
  EXPECT_EQ(std::accumulate(ch.begin(), ch.end(), 0), 300);
}

}  // namespace
