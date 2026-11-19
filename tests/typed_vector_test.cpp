#include <nlib/typed_vector.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

namespace {

using apple_index = nlib::strong_int<struct apple_index_tag, std::uint32_t>;
using pear_index = nlib::strong_int<struct pear_index_tag, std::uint32_t>;

static_assert(!std::is_copy_constructible_v<nlib::typed_vector<apple_index, int>>);
static_assert(!std::is_copy_assignable_v<nlib::typed_vector<apple_index, int>>);
static_assert(std::is_nothrow_move_constructible_v<nlib::typed_vector<apple_index, int>>);

template <typename Vector, typename Index>
concept indexable_by = requires(Vector vector, Index index) { vector[index]; };
static_assert(indexable_by<nlib::typed_vector<apple_index, int>, apple_index>);
static_assert(!indexable_by<nlib::typed_vector<apple_index, int>, pear_index>);
static_assert(!indexable_by<nlib::typed_vector<apple_index, int>, std::uint32_t>);

TEST(TypedVector, EmplaceBackReturnsConsecutiveIndexes) {
  nlib::typed_vector<apple_index, std::string> apples;
  EXPECT_TRUE(apples.empty());

  EXPECT_EQ(apples.emplace_back("fuji"), apple_index{0});
  EXPECT_EQ(apples.emplace_back(3, 'x'), apple_index{1});

  EXPECT_EQ(apples.size(), 2u);
  EXPECT_EQ(apples[apple_index{0}], "fuji");
  EXPECT_EQ(apples[apple_index{1}], "xxx");
}

TEST(TypedVector, IteratesInIndexOrder) {
  nlib::typed_vector<apple_index, int> digits;
  digits.emplace_back(7);
  digits.emplace_back(9);

  int number = 0;
  for (const int digit : digits) number = number * 10 + digit;
  EXPECT_EQ(number, 79);
}

TEST(TypedVector, MovesWithItsMoveOnlyElements) {
  nlib::typed_vector<apple_index, std::unique_ptr<int>> owned;
  owned.emplace_back(std::make_unique<int>(7));
  const apple_index second = owned.emplace_back(std::make_unique<int>(9));

  const nlib::typed_vector<apple_index, std::unique_ptr<int>> moved = std::move(owned);
  EXPECT_EQ(*moved[second], 9);
}

}  // namespace
