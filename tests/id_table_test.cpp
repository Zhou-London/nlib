#include <nlib/id_table.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

using city_id = nlib::strong_int<struct city_id_tag, std::int64_t>;
using city_index = nlib::strong_int<struct city_index_tag, std::uint32_t>;

struct city {
  city_id id;
  std::string name;
};

TEST(IdTable, InsertAssignsDenseIndexesInInsertionOrder) {
  nlib::id_table<city_index, city> cities;
  EXPECT_TRUE(cities.empty());

  EXPECT_EQ(cities.insert({city_id{900}, "north"}), city_index{0});
  EXPECT_EQ(cities.insert({city_id{17}, "south"}), city_index{1});

  EXPECT_EQ(cities.size(), 2u);
  EXPECT_EQ(cities[city_index{0}].name, "north");
  EXPECT_EQ(cities[city_index{1}].id, city_id{17});
}

TEST(IdTable, FindMapsAnIdToItsIndex) {
  nlib::id_table<city_index, city> cities;
  cities.insert({city_id{900}, "north"});
  cities.insert({city_id{17}, "south"});

  EXPECT_EQ(cities.find(city_id{17}), city_index{1});
  EXPECT_EQ(cities.find(city_id{900}), city_index{0});
  EXPECT_EQ(cities.find(city_id{1}), std::nullopt);
}

TEST(IdTable, IteratesRecordsInIndexOrder) {
  nlib::id_table<city_index, city> cities;
  cities.insert({city_id{900}, "north"});
  cities.insert({city_id{17}, "south"});

  std::string names;
  for (const city& each : cities) names += each.name + " ";
  EXPECT_EQ(names, "north south ");
}

}  // namespace
