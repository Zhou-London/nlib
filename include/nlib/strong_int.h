#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>

namespace nlib {

template <typename Tag, std::integral Rep>
class strong_int {
 public:
  using rep = Rep;

  constexpr strong_int() noexcept = default;
  constexpr explicit strong_int(Rep value) noexcept : value_(value) {}

  constexpr Rep value() const noexcept { return value_; }

  friend constexpr auto operator<=>(strong_int, strong_int) noexcept = default;

 private:
  Rep value_{};
};

}  // namespace nlib

template <typename Tag, std::integral Rep>
struct std::hash<nlib::strong_int<Tag, Rep>> {
  std::size_t operator()(nlib::strong_int<Tag, Rep> id) const noexcept {
    return std::hash<Rep>{}(id.value());
  }
};
