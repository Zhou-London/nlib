#include <nlib/single_queue.h>

#include <gtest/gtest.h>

#include <memory>
#include <thread>

namespace {

// Counts live instances to check that elements are constructed/destroyed once.
struct Counted {
  static int alive;
  int value;

  explicit Counted(int v) : value(v) { ++alive; }
  Counted(const Counted& other) : value(other.value) { ++alive; }
  Counted(Counted&& other) noexcept : value(other.value) { ++alive; }
  ~Counted() { --alive; }
};
int Counted::alive = 0;

TEST(SingleQueue, DefaultStateIsEmpty) {
  nq::single_queue<int> q(4);
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.size(), 0u);
  EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(SingleQueue, CapacityRoundsUpToPowerOfTwo) {
  EXPECT_EQ(nq::single_queue<int>(0).capacity(), 1u);
  EXPECT_EQ(nq::single_queue<int>(1).capacity(), 1u);
  EXPECT_EQ(nq::single_queue<int>(5).capacity(), 8u);
  EXPECT_EQ(nq::single_queue<int>(8).capacity(), 8u);
  EXPECT_EQ(nq::single_queue<int>(9).capacity(), 16u);
}

TEST(SingleQueue, PopsInFifoOrder) {
  nq::single_queue<int> q(8);
  for (int i = 0; i < 8; ++i) EXPECT_TRUE(q.try_push(i));
  for (int i = 0; i < 8; ++i) EXPECT_EQ(q.try_pop(), i);
  EXPECT_TRUE(q.empty());
}

TEST(SingleQueue, TryPushFailsWhenFull) {
  nq::single_queue<int> q(4);
  for (int i = 0; i < 4; ++i) EXPECT_TRUE(q.try_push(i));
  EXPECT_FALSE(q.try_push(99));
  EXPECT_EQ(q.size(), 4u);
  EXPECT_EQ(q.try_pop(), 0);
  EXPECT_TRUE(q.try_push(99));
}

TEST(SingleQueue, FailedPushLeavesMoveOnlyArgumentIntact) {
  nq::single_queue<std::unique_ptr<int>> q(1);
  EXPECT_TRUE(q.try_push(std::make_unique<int>(1)));

  auto p = std::make_unique<int>(2);
  EXPECT_FALSE(q.try_push(std::move(p)));
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(*p, 2);
}

TEST(SingleQueue, WrapAroundManyTimes) {
  nq::single_queue<int> q(4);
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(q.try_push(i));
    ASSERT_EQ(q.try_pop(), i);
  }
  EXPECT_TRUE(q.empty());
}

TEST(SingleQueue, ElementsAreDestroyedExactlyOnce) {
  ASSERT_EQ(Counted::alive, 0);
  {
    nq::single_queue<Counted> q(8);
    for (int i = 0; i < 6; ++i) q.try_emplace(i);
    EXPECT_EQ(Counted::alive, 6);

    EXPECT_EQ(q.try_pop()->value, 0);
    EXPECT_EQ(Counted::alive, 5);
  }  // The five queued elements are destroyed by the destructor.
  EXPECT_EQ(Counted::alive, 0);
}

TEST(SingleQueue, SpscStressPreservesOrderAndValues) {
  constexpr int n = 200000;
  nq::single_queue<int> q(4);  // small capacity forces constant wrap and contention

  std::thread producer([&q] {
    for (int i = 0; i < n; ++i)
      while (!q.try_push(i)) std::this_thread::yield();
  });

  int expected = 0;
  while (expected < n) {
    if (auto v = q.try_pop()) {
      ASSERT_EQ(*v, expected);
      ++expected;
    }
  }
  producer.join();
  EXPECT_TRUE(q.empty());
}

TEST(SingleQueue, SpscStressWithMoveOnlyElements) {
  constexpr int n = 50000;
  nq::single_queue<std::unique_ptr<int>> q(16);

  std::thread producer([&q] {
    for (int i = 0; i < n; ++i) {
      auto p = std::make_unique<int>(i);
      while (!q.try_push(std::move(p))) std::this_thread::yield();
    }
  });

  int expected = 0;
  while (expected < n) {
    if (auto v = q.try_pop()) {
      ASSERT_EQ(**v, expected);
      ++expected;
    }
  }
  producer.join();
}

}  // namespace
