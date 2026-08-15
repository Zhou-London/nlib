#include <nlib/pool.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
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

struct ThrowingCtor {
  explicit ThrowingCtor(bool should_throw) {
    if (should_throw) throw std::runtime_error("ctor");
  }
};

static_assert(!std::is_copy_constructible_v<nlib::pool<int>>);
static_assert(!std::is_copy_assignable_v<nlib::pool<int>>);
static_assert(!std::is_move_constructible_v<nlib::pool<int>>);
static_assert(!std::is_move_assignable_v<nlib::pool<int>>);

TEST(Pool, StartsEmptyWithRequestedCapacity) {
  nlib::pool<int> p(16);
  EXPECT_TRUE(p.empty());
  EXPECT_EQ(p.size(), 0u);
  EXPECT_GE(p.capacity(), 16u);
}

TEST(Pool, EmplaceReturnsHandleForAccess) {
  nlib::pool<std::string> p(4);
  const auto a = p.emplace("alpha");
  const auto b = p.emplace(3, 'b');
  EXPECT_EQ(p[a], "alpha");
  EXPECT_EQ(p[b], "bbb");
  EXPECT_EQ(p.size(), 2u);
  EXPECT_FALSE(p.empty());

  p[a] = "changed";
  EXPECT_EQ(p[a], "changed");
}

TEST(Pool, ReleaseRecyclesMostRecentSlotFirst) {
  nlib::pool<int> p(8);
  const auto a = p.emplace(1);
  const auto b = p.emplace(2);
  const auto c = p.emplace(3);

  p.release(b);
  p.release(a);
  EXPECT_EQ(p.size(), 1u);

  EXPECT_EQ(p.emplace(4), a);
  EXPECT_EQ(p.emplace(5), b);
  EXPECT_EQ(p[a], 4);
  EXPECT_EQ(p[b], 5);
  EXPECT_EQ(p[c], 3);
}

TEST(Pool, GrowsBeyondInitialCapacity) {
  nlib::pool<int> p(2);
  std::vector<std::size_t> handles;
  for (int i = 0; i < 100; ++i) handles.push_back(p.emplace(i));
  EXPECT_EQ(p.size(), 100u);
  EXPECT_GE(p.capacity(), 100u);
  for (int i = 0; i < 100; ++i) EXPECT_EQ(p[handles[static_cast<std::size_t>(i)]], i);
}

TEST(Pool, GrowthRelocatesLiveAndFreeSlots) {
  nlib::pool<std::string> p(4);
  std::vector<std::size_t> handles;
  for (int i = 0; i < 4; ++i)
    handles.push_back(p.emplace("value-long-enough-to-defeat-sso-" + std::to_string(i)));
  p.release(handles[1]);
  p.release(handles[2]);

  for (int i = 4; i < 64; ++i)  // reuses the two free slots, then grows
    handles.push_back(p.emplace("value-long-enough-to-defeat-sso-" + std::to_string(i)));

  EXPECT_EQ(p.size(), 62u);
  EXPECT_EQ(p[handles[0]], "value-long-enough-to-defeat-sso-0");
  EXPECT_EQ(p[handles[2]], "value-long-enough-to-defeat-sso-4");
  EXPECT_EQ(p[handles[1]], "value-long-enough-to-defeat-sso-5");
  for (int i = 6; i < 64; ++i)
    EXPECT_EQ(p[handles[static_cast<std::size_t>(i)]],
              "value-long-enough-to-defeat-sso-" + std::to_string(i));
}

TEST(Pool, MoveOnlyElements) {
  nlib::pool<std::unique_ptr<int>> p(2);
  const auto a = p.emplace(std::make_unique<int>(7));
  for (int i = 0; i < 20; ++i) p.emplace(std::make_unique<int>(i));  // growth relocates
  EXPECT_EQ(*p[a], 7);
}

TEST(Pool, ElementsAreDestroyedExactlyOnce) {
  ASSERT_EQ(Counted::alive, 0);
  {
    nlib::pool<Counted> p(4);
    std::vector<std::size_t> handles;
    for (int i = 0; i < 10; ++i) handles.push_back(p.emplace(i));  // grows past 4
    EXPECT_EQ(Counted::alive, 10);

    p.release(handles[3]);
    p.release(handles[7]);
    EXPECT_EQ(Counted::alive, 8);

    p.emplace(42);
    EXPECT_EQ(Counted::alive, 9);
  }
  EXPECT_EQ(Counted::alive, 0);
}

TEST(Pool, FailedConstructionAddsNothing) {
  nlib::pool<ThrowingCtor> p(2);
  const auto a = p.emplace(false);
  EXPECT_THROW(p.emplace(true), std::runtime_error);  // append path
  EXPECT_EQ(p.size(), 1u);

  p.release(a);
  EXPECT_THROW(p.emplace(true), std::runtime_error);  // free-list path
  EXPECT_TRUE(p.empty());
  EXPECT_EQ(p.emplace(false), a);  // the slot survived the failed attempt
}

}  // namespace
