#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nlib {

// Prices carry 10 and quantities 8 fixed-point decimals: enough for every
// Kraken spot pair (up to 10 price and 8 lot decimals). At these scales an
// int64 holds prices to ~9.2e8 quote units and quantities to ~9.2e10 units.
inline constexpr std::int64_t price_scale = 10'000'000'000;
inline constexpr std::int64_t qty_scale = 100'000'000;

inline constexpr std::size_t book_depth = 10;

enum class side : std::uint8_t { buy, sell };

enum class order_type : std::uint8_t { limit, market };

// What an order record does to the book. `qty` means: for add, the resting
// quantity; for cancel, the cancelled quantity (the order leaves the book at
// zero remaining); for modify, the new remaining quantity. clear drops every
// resting order of the instrument — a feed sends it before replaying a
// snapshot — and only `seq`, `instrument_id` and the times are meaningful.
enum class order_action : std::uint8_t { add, cancel, modify, clear };

struct order {
  std::int64_t seq;       // feed sequence number
  std::int64_t order_id;
  std::int64_t price;     // fixed-point, 1/price_scale of the quote unit
  std::int64_t qty;       // fixed-point, 1/qty_scale trading units; meaning set by `action`
  std::int64_t event_ns;  // exchange event time, Unix-epoch nanoseconds
  order* prev;            // intrusive list hooks, written by the owning book
  order* next;
  std::uint32_t instrument_id;  // mapping is application-defined
  nlib::side side;              // qualified: the member name hides the enum in class scope
  order_type type;
  order_action action;
  std::int64_t recv_ns;   // local receive time, stamped by the receiving process
};

struct trade {
  std::int64_t seq;       // feed sequence number
  std::int64_t buy_order_id;
  std::int64_t sell_order_id;
  std::int64_t price;     // fixed-point, 1/price_scale of the quote unit
  std::int64_t qty;       // fixed-point, 1/qty_scale trading units
  std::int64_t event_ns;  // exchange event time, Unix-epoch nanoseconds
  std::uint32_t instrument_id;  // mapping is application-defined
  nlib::side side;        // aggressor side; qualified as in order
  std::int64_t recv_ns;   // local receive time, stamped by the receiving process
};

struct book {
  std::int64_t event_ns;                // event time of the latest applied event
  std::int64_t bid_price[book_depth];   // best first; fixed-point, 1/price_scale; 0 if unused
  std::int64_t bid_qty[book_depth];     // fixed-point, 1/qty_scale trading units; 0 if unused
  std::int64_t ask_price[book_depth];   // best first; fixed-point, 1/price_scale; 0 if unused
  std::int64_t ask_qty[book_depth];     // fixed-point, 1/qty_scale trading units; 0 if unused
  std::uint32_t instrument_id;          // mapping is application-defined
  std::int64_t recv_ns;                 // receive time of the latest applied event
};

static_assert(std::is_trivially_copyable_v<order> && std::is_standard_layout_v<order>);
static_assert(std::is_trivially_copyable_v<trade> && std::is_standard_layout_v<trade>);
static_assert(std::is_trivially_copyable_v<book> && std::is_standard_layout_v<book>);

// The wire contract: feeds serialize these structs byte for byte (LP64,
// little-endian), so any layout drift must fail the build, not the peer.
static_assert(sizeof(order) == 72);
static_assert(sizeof(trade) == 64);
static_assert(sizeof(book) == 344);

}  // namespace nlib
