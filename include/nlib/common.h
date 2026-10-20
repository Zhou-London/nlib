#pragma once

#include <cstdint>
#include <type_traits>

#ifndef NLIB_CACHE_LINE
#define NLIB_CACHE_LINE 64
#endif

namespace nlib {

using order_id = std::uint64_t;
using price_t = std::int64_t;
using qty_t = std::int64_t;
using ns_t = std::int64_t;

struct level;

struct order {
  order_id id;
  price_t price;
  qty_t qty;
  ns_t ts;
  std::uint8_t side;
  order* prev;
  order* next;
  level* level;
};

struct level {
  price_t price;
  qty_t qty;
  std::uint32_t count;
  order* head;
  order* tail;
};

static_assert(std::is_trivially_copyable_v<order> && std::is_standard_layout_v<order>);
static_assert(std::is_trivially_copyable_v<level> && std::is_standard_layout_v<level>);

}  // namespace nlib
