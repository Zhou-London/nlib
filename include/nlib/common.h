#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nlib {

inline constexpr std::int64_t price_scale = 10'000;
inline constexpr std::size_t book_depth = 10;

enum class side : std::uint8_t { buy, sell };

enum class order_type : std::uint8_t { limit, market };

enum class order_action : std::uint8_t { add, cancel };

struct order {
  std::int64_t seq;       // feed sequence number
  std::int64_t order_id;
  std::int64_t price;     // fixed-point, 1/price_scale of the quote unit
  std::int64_t qty;       // trading units; for order_action::cancel, the cancelled quantity
  std::int64_t time_ns;   // Unix-epoch nanoseconds
  order* prev;            // intrusive list hooks, written by the owning book
  order* next;
  std::uint32_t instrument_id;  // mapping is application-defined
  side side;
  order_type type;
  order_action action;
};

struct trade {
  std::int64_t seq;       // feed sequence number
  std::int64_t buy_order_id;
  std::int64_t sell_order_id;
  std::int64_t price;     // fixed-point, 1/price_scale of the quote unit
  std::int64_t qty;       // trading units
  std::int64_t time_ns;   // Unix-epoch nanoseconds
  std::uint32_t instrument_id;  // mapping is application-defined
  side side;              // aggressor side
};

struct book {
  std::int64_t time_ns;                 // Unix-epoch nanoseconds
  std::int64_t bid_price[book_depth];   // best first; fixed-point, 1/price_scale; 0 if unused
  std::int64_t bid_qty[book_depth];     // trading units; 0 if unused
  std::int64_t ask_price[book_depth];   // best first; fixed-point, 1/price_scale; 0 if unused
  std::int64_t ask_qty[book_depth];     // trading units; 0 if unused
  std::uint32_t instrument_id;          // mapping is application-defined
};

static_assert(std::is_trivially_copyable_v<order> && std::is_standard_layout_v<order>);
static_assert(std::is_trivially_copyable_v<trade> && std::is_standard_layout_v<trade>);
static_assert(std::is_trivially_copyable_v<book> && std::is_standard_layout_v<book>);

}  // namespace nlib
