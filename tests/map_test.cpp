#include <nlib/map.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// Counts live instances to check that elements are constructed/destroyed once.
struct Counted {
  static int alive;
  int value;

  explicit Counted(int v) : value(v) { ++alive; }
  Counted(Counted&& other) noexcept : value(other.value) { ++alive; }
  Counted& operator=(Counted&&) = default;
  ~Counted() { --alive; }
};
int Counted::alive = 0;

static_assert(!std::is_copy_constructible_v<nq::map<int, int>>);
static_assert(!std::is_copy_assignable_v<nq::map<int, int>>);
static_assert(std::is_move_constructible_v<nq::map<int, int>>);
static_assert(std::is_move_assignable_v<nq::map<int, int>>);

TEST(Map, DefaultConstructedIsEmpty) {
  nq::map<int, int> m;
  EXPECT_TRUE(m.empty());
  EXPECT_EQ(m.size(), 0u);
  EXPECT_EQ(m.begin(), m.end());
  EXPECT_EQ(m.find(1), m.end());
  EXPECT_FALSE(m.contains(1));
  EXPECT_EQ(m.erase(1), 0u);
}

TEST(Map, TryEmplaceInsertsOnce) {
  nq::map<int, std::string> m;
  auto [it, inserted] = m.try_emplace(1, "one");
  EXPECT_TRUE(inserted);
  EXPECT_EQ(it->first, 1);
  EXPECT_EQ(it->second, "one");

  auto [it2, inserted2] = m.try_emplace(1, "uno");
  EXPECT_FALSE(inserted2);
  EXPECT_EQ(it2, it);
  EXPECT_EQ(m.at(1), "one");
  EXPECT_EQ(m.size(), 1u);
}

TEST(Map, TryEmplaceLeavesArgumentsOnFailure) {
  nq::map<std::string, std::unique_ptr<int>> m;
  std::string key = "a-key-long-enough-to-defeat-sso";
  EXPECT_TRUE(m.try_emplace(std::move(key), std::make_unique<int>(1)).second);

  key = "a-key-long-enough-to-defeat-sso";
  auto value = std::make_unique<int>(2);
  EXPECT_FALSE(m.try_emplace(std::move(key), std::move(value)).second);
  EXPECT_EQ(key, "a-key-long-enough-to-defeat-sso");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 2);
}

TEST(Map, InsertMovesPairOnlyOnSuccess) {
  nq::map<int, std::unique_ptr<int>> m;
  EXPECT_TRUE(m.insert({7, std::make_unique<int>(70)}).second);

  std::pair<int, std::unique_ptr<int>> dup(7, std::make_unique<int>(71));
  EXPECT_FALSE(m.insert(std::move(dup)).second);
  ASSERT_NE(dup.second, nullptr);
  EXPECT_EQ(*dup.second, 71);
  EXPECT_EQ(*m.at(7), 70);
}

TEST(Map, EmplaceConstructsPair) {
  nq::map<int, std::string> m;
  auto [it, inserted] = m.emplace(3, "three");
  EXPECT_TRUE(inserted);
  EXPECT_EQ(it->second, "three");
  EXPECT_FALSE(m.emplace(3, "drei").second);
  EXPECT_EQ(m.at(3), "three");
}

TEST(Map, SubscriptInsertsDefaultAndAssigns) {
  nq::map<int, std::string> m;
  m[5] = "five";
  EXPECT_EQ(m.at(5), "five");
  EXPECT_EQ(m[6], "");
  EXPECT_EQ(m.size(), 2u);
  m[5] = "cinq";
  EXPECT_EQ(m.at(5), "cinq");
  EXPECT_EQ(m.size(), 2u);
}

TEST(Map, AtThrowsForMissingKey) {
  nq::map<int, int> m;
  m.try_emplace(1, 10);
  EXPECT_EQ(m.at(1), 10);
  EXPECT_THROW(m.at(2), std::out_of_range);
  const auto& cm = m;
  EXPECT_THROW(cm.at(2), std::out_of_range);
}

TEST(Map, GrowthKeepsAllElementsFindable) {
  nq::map<int, int> m;
  for (int i = 0; i < 10000; ++i) EXPECT_TRUE(m.try_emplace(i * 7, i).second);
  EXPECT_EQ(m.size(), 10000u);
  for (int i = 0; i < 10000; ++i) {
    auto it = m.find(i * 7);
    ASSERT_NE(it, m.end());
    EXPECT_EQ(it->second, i);
  }
  EXPECT_FALSE(m.contains(-1));
}

TEST(Map, IterationVisitsEachElementOnce) {
  nq::map<int, int> m;
  for (int i = 0; i < 100; ++i) m.emplace(i, i * 2);

  std::vector<int> keys;
  for (const auto& [k, v] : m) {
    EXPECT_EQ(v, k * 2);
    keys.push_back(k);
  }
  std::sort(keys.begin(), keys.end());
  std::vector<int> expected(100);
  for (int i = 0; i < 100; ++i) expected[static_cast<std::size_t>(i)] = i;
  EXPECT_EQ(keys, expected);
}

TEST(Map, EraseByKey) {
  nq::map<int, int> m;
  for (int i = 0; i < 50; ++i) m.emplace(i, i);

  for (int i = 0; i < 50; i += 2) EXPECT_EQ(m.erase(i), 1u);
  EXPECT_EQ(m.size(), 25u);
  for (int i = 0; i < 50; ++i) EXPECT_EQ(m.contains(i), i % 2 == 1);
  EXPECT_EQ(m.erase(2), 0u);
}

TEST(Map, EraseIteratorReturnsNext) {
  nq::map<int, int> m;
  for (int i = 0; i < 20; ++i) m.emplace(i, i);

  std::size_t visited = 0;
  for (auto it = m.begin(); it != m.end();) {
    it = m.erase(it);
    ++visited;
  }
  EXPECT_EQ(visited, 20u);
  EXPECT_TRUE(m.empty());
}

TEST(Map, ErasedSlotsAreReusable) {
  nq::map<int, int> m;
  m.reserve(64);
  for (int round = 0; round < 100; ++round) {
    for (int i = 0; i < 64; ++i) ASSERT_TRUE(m.emplace(i, round).second);
    for (int i = 0; i < 64; ++i) ASSERT_EQ(m.erase(i), 1u);
  }
  EXPECT_TRUE(m.empty());
}

TEST(Map, ReserveKeepsReferencesStable) {
  nq::map<int, int> m;
  m.reserve(100);
  m.try_emplace(0, 0);
  const int* addr = &m.at(0);
  for (int i = 1; i < 100; ++i) m.emplace(i, i);
  EXPECT_EQ(&m.at(0), addr);
}

TEST(Map, ClearFreesAndStaysUsable) {
  nq::map<int, int> m;
  for (int i = 0; i < 100; ++i) m.emplace(i, i);

  m.clear();
  EXPECT_TRUE(m.empty());
  EXPECT_EQ(m.begin(), m.end());

  EXPECT_TRUE(m.try_emplace(1, 10).second);
  EXPECT_EQ(m.at(1), 10);
  EXPECT_EQ(m.size(), 1u);
}

TEST(Map, MoveConstructAndAssign) {
  nq::map<int, int> m;
  for (int i = 0; i < 30; ++i) m.emplace(i, i);

  nq::map<int, int> moved(std::move(m));
  EXPECT_EQ(moved.size(), 30u);
  for (int i = 0; i < 30; ++i) EXPECT_EQ(moved.at(i), i);

  nq::map<int, int> assigned;
  assigned.try_emplace(99, 99);
  assigned = std::move(moved);
  EXPECT_EQ(assigned.size(), 30u);
  EXPECT_FALSE(assigned.contains(99));
}

TEST(Map, SwapExchangesContents) {
  nq::map<int, int> a;
  a.try_emplace(1, 10);
  nq::map<int, int> b;
  b.try_emplace(2, 20);
  b.try_emplace(3, 30);

  swap(a, b);
  EXPECT_EQ(a.size(), 2u);
  EXPECT_EQ(a.at(2), 20);
  EXPECT_EQ(b.size(), 1u);
  EXPECT_EQ(b.at(1), 10);
}

TEST(Map, ElementsAreDestroyedExactlyOnce) {
  ASSERT_EQ(Counted::alive, 0);
  {
    nq::map<int, Counted> m;
    for (int i = 0; i < 50; ++i) m.emplace(i, i);  // grows past several rehashes
    EXPECT_EQ(Counted::alive, 50);

    m.erase(7);
    EXPECT_EQ(Counted::alive, 49);

    m.clear();
    EXPECT_EQ(Counted::alive, 0);

    for (int i = 0; i < 10; ++i) m.emplace(i, i);
    EXPECT_EQ(Counted::alive, 10);
  }
  EXPECT_EQ(Counted::alive, 0);
}

TEST(Map, RandomOpsMatchStdUnorderedMap) {
  nq::map<int, int> m;
  std::unordered_map<int, int> ref;
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> key_dist(0, 500);

  for (int step = 0; step < 100000; ++step) {
    int key = key_dist(rng);
    switch (rng() % 4) {
      case 0:
      case 1: {
        const int value = static_cast<int>(rng() % 1000);
        EXPECT_EQ(m.try_emplace(std::move(key), value).second,
                  ref.try_emplace(key, value).second);
        break;
      }
      case 2:
        EXPECT_EQ(m.erase(key), ref.erase(key));
        break;
      case 3: {
        const auto it = m.find(key);
        const auto ref_it = ref.find(key);
        ASSERT_EQ(it == m.end(), ref_it == ref.end());
        if (it != m.end()) EXPECT_EQ(it->second, ref_it->second);
        break;
      }
    }
    ASSERT_EQ(m.size(), ref.size());
  }

  std::vector<std::pair<int, int>> got(m.begin(), m.end());
  std::vector<std::pair<int, int>> expected(ref.begin(), ref.end());
  std::sort(got.begin(), got.end());
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(got, expected);
}

}  // namespace
