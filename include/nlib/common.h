#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace nlib {

// ---------- Constants ----------
inline constexpr std::int64_t price_scale = 10'000'000'000;
inline constexpr std::int64_t qty_scale = 100'000'000;

inline constexpr std::uint8_t order_tag = 0;
inline constexpr std::uint8_t trade_tag = 1;
inline constexpr std::uint8_t level_tag = 2;
inline constexpr std::uint8_t cancel_tag = 3;

// ---------- Enum ----------
enum class side : std::uint8_t { buy, sell };
enum class order_type : std::uint8_t { limit, market };
enum class order_action : std::uint8_t { add, modify };

// ---------- Structures ----------
struct order {
  std::int64_t seq;
  std::int64_t order_id;
  std::int64_t price; // fixed-point
  std::int64_t qty;
  std::int64_t new_qty;
  order *prev; // intrusive list
  order *next;
  std::uint32_t instrument_id;
  nlib::side side;
  order_type type;
  order_action action;
  std::int64_t event_ns;
  std::int64_t recv_ns; // local receive time
};

struct cancel {
  std::int64_t seq;
  std::int64_t order_id;
  std::int64_t qty; // quantity leaving the book
  std::uint32_t instrument_id;
  nlib::side side;
  std::int64_t event_ns;
  std::int64_t recv_ns; // local receive time
};

struct trade {
  std::int64_t seq;
  std::int64_t buy_order_id;
  std::int64_t sell_order_id;
  std::int64_t price;
  std::int64_t qty;
  std::int64_t event_ns;
  std::uint32_t instrument_id;
  nlib::side side;
  std::int64_t recv_ns;
};

struct level {
  std::int64_t seq;
  std::int64_t price;
  std::int64_t qty;
  std::int64_t event_ns;
  std::uint32_t instrument_id;
  nlib::side side;
  std::int64_t recv_ns;
};

struct book {
  std::int64_t event_ns;
  std::int64_t bid_price[10];
  std::int64_t bid_qty[10];
  std::int64_t ask_price[10];
  std::int64_t ask_qty[10];
  std::uint32_t instrument_id;
  std::int64_t recv_ns;
};

struct metrics {
  std::int64_t ts_ns;
  std::uint64_t feed_messages;
  std::uint64_t feed_bytes;
  std::uint64_t feed_orders;
  std::uint64_t feed_trades;
  std::uint64_t feed_levels;
  std::uint64_t feed_dropped;
  std::uint64_t book_events;
  std::uint64_t book_apply_ns;
  std::uint64_t book_samples;
  std::uint64_t book_instruments;
  std::uint64_t book_resting_orders;
  std::uint64_t book_memory_bytes;
  std::uint64_t writer_orders;
  std::uint64_t writer_trades;
  std::uint64_t writer_levels;
  std::uint64_t writer_books;
};

struct price_level {
  std::int64_t qty = 0;
  order *head = nullptr;
  order *tail = nullptr;
};

static_assert(std::is_trivially_copyable_v<order> &&
              std::is_standard_layout_v<order>);
static_assert(std::is_trivially_copyable_v<cancel> &&
              std::is_standard_layout_v<cancel>);
static_assert(std::is_trivially_copyable_v<trade> &&
              std::is_standard_layout_v<trade>);
static_assert(std::is_trivially_copyable_v<level> &&
              std::is_standard_layout_v<level>);
static_assert(std::is_trivially_copyable_v<book> &&
              std::is_standard_layout_v<book>);
static_assert(std::is_trivially_copyable_v<metrics> &&
              std::is_standard_layout_v<metrics>);

// ---------- Alias ----------
using feed_event = std::variant<order, trade, level, cancel>;
using record = std::variant<order, trade, level, cancel, book>;

template <typename... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

} // namespace nlib
