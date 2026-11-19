#include <nlib/strong_int.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <unordered_set>

namespace {

using apple_index = nlib::strong_int<struct apple_index_tag, std::uint32_t>;
using pear_index = nlib::strong_int<struct pear_index_tag, std::uint32_t>;

static_assert(!std::is_convertible_v<std::uint32_t, apple_index>);
static_assert(!std::is_convertible_v<apple_index, std::uint32_t>);
static_assert(!std::is_convertible_v<apple_index, pear_index>);
static_assert(!std::is_constructible_v<apple_index, pear_index>);
static_assert(!std::equality_comparable_with<apple_index, pear_index>);
static_assert(std::is_trivially_copyable_v<apple_index>);
static_assert(sizeof(apple_index) == sizeof(std::uint32_t));

TEST(StrongInt, DefaultsToZero) { EXPECT_EQ(apple_index{}.value(), 0u); }

TEST(StrongInt, ComparesByValue) {
  EXPECT_EQ(apple_index{3}, apple_index{3});
  EXPECT_NE(apple_index{3}, apple_index{4});
  EXPECT_LT(apple_index{3}, apple_index{4});
}

TEST(StrongInt, HashesByValue) {
  const std::unordered_set<apple_index> seen{apple_index{1}, apple_index{2}, apple_index{1}};
  EXPECT_EQ(seen.size(), 2u);
}

}  // namespace
