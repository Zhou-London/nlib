#include <nlib/common.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <variant>

namespace {

TEST(Common, TagsSeparateTheFeedRecords) {
  EXPECT_NE(nlib::order_tag, nlib::trade_tag);
  EXPECT_NE(nlib::order_tag, nlib::level_tag);
  EXPECT_NE(nlib::trade_tag, nlib::level_tag);
  EXPECT_NE(nlib::cancel_tag, nlib::order_tag);
  EXPECT_NE(nlib::cancel_tag, nlib::trade_tag);
  EXPECT_NE(nlib::cancel_tag, nlib::level_tag);
  EXPECT_EQ(sizeof(nlib::order_tag), 1u);
}

TEST(Common, FeedEventHoldsTheRecordItIsAssigned) {
  nlib::feed_event event = nlib::order{.order_id = 7};
  EXPECT_TRUE(std::holds_alternative<nlib::order>(event));
  EXPECT_EQ(std::get<nlib::order>(event).order_id, 7);

  event = nlib::trade{.seq = 3};
  EXPECT_TRUE(std::holds_alternative<nlib::trade>(event));
  EXPECT_EQ(std::get<nlib::trade>(event).seq, 3);

  event = nlib::level{.price = 5, .qty = 9};
  EXPECT_TRUE(std::holds_alternative<nlib::level>(event));
  EXPECT_EQ(std::get<nlib::level>(event).qty, 9);

  event = nlib::cancel{.order_id = 7, .qty = 2};
  EXPECT_TRUE(std::holds_alternative<nlib::cancel>(event));
  EXPECT_EQ(std::get<nlib::cancel>(event).qty, 2);
}

TEST(Common, RecordAddsTheSnapshotToTheFeedRecords) {
  EXPECT_EQ(std::variant_size_v<nlib::record>, std::variant_size_v<nlib::feed_event> + 1);

  nlib::record r = nlib::book{.instrument_id = 42};
  EXPECT_TRUE(std::holds_alternative<nlib::book>(r));
  EXPECT_EQ(std::get<nlib::book>(r).instrument_id, 42u);
}

TEST(Common, OverloadedDispatchesEveryAlternative) {
  const auto name = [](const nlib::record& r) {
    return std::visit(nlib::overloaded{[](const nlib::order&) { return std::string("order"); },
                                       [](const nlib::trade&) { return std::string("trade"); },
                                       [](const nlib::level&) { return std::string("level"); },
                                       [](const nlib::cancel&) { return std::string("cancel"); },
                                       [](const nlib::book&) { return std::string("book"); }},
                      r);
  };
  EXPECT_EQ(name(nlib::order{}), "order");
  EXPECT_EQ(name(nlib::trade{}), "trade");
  EXPECT_EQ(name(nlib::level{}), "level");
  EXPECT_EQ(name(nlib::cancel{}), "cancel");
  EXPECT_EQ(name(nlib::book{}), "book");
}

TEST(Common, OverloadedPicksTheCatchAllForUnlistedAlternatives) {
  const auto is_trade = [](const nlib::record& r) {
    return std::visit(nlib::overloaded{[](const nlib::trade&) { return true; },
                                       [](const auto&) { return false; }},
                      r);
  };
  EXPECT_TRUE(is_trade(nlib::trade{}));
  EXPECT_FALSE(is_trade(nlib::order{}));
  EXPECT_FALSE(is_trade(nlib::level{}));
  EXPECT_FALSE(is_trade(nlib::cancel{}));
  EXPECT_FALSE(is_trade(nlib::book{}));
}

TEST(PriceLevel, DefaultsToTheAggregateState) {
  nlib::price_level level;
  EXPECT_EQ(level.qty, 0);
  EXPECT_EQ(level.head, nullptr);
  EXPECT_EQ(level.tail, nullptr);
}

TEST(PriceLevel, QueuesOrdersThroughTheirHooks) {
  nlib::order first{.order_id = 1, .qty = 5};
  nlib::order second{.order_id = 2, .qty = 3};
  nlib::price_level level;

  for (nlib::order* n : {&first, &second}) {
    n->prev = level.tail;
    n->next = nullptr;
    (level.tail ? level.tail->next : level.head) = n;
    level.tail = n;
    level.qty += n->qty;
  }

  EXPECT_EQ(level.qty, 8);
  EXPECT_EQ(level.head, &first);
  EXPECT_EQ(level.tail, &second);
  EXPECT_EQ(level.head->next, &second);
  EXPECT_EQ(level.tail->prev, &first);
}

}  // namespace
