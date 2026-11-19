#pragma once

#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

#include <nlib/strong_int.h>

namespace nlib {

template <typename Index, typename T>
  requires std::unsigned_integral<typename Index::rep>
class typed_vector {
 public:
  using index_type = Index;
  using value_type = T;
  using size_type = std::size_t;
  using iterator = typename std::vector<T>::iterator;
  using const_iterator = typename std::vector<T>::const_iterator;

  typed_vector() noexcept = default;
  typed_vector(typed_vector&&) noexcept = default;
  typed_vector& operator=(typed_vector&&) noexcept = default;
  typed_vector(const typed_vector&) = delete;
  typed_vector& operator=(const typed_vector&) = delete;

  T& operator[](Index index) noexcept { return elements_[index.value()]; }
  const T& operator[](Index index) const noexcept { return elements_[index.value()]; }

  [[nodiscard]] bool empty() const noexcept { return elements_.empty(); }
  size_type size() const noexcept { return elements_.size(); }
  void reserve(size_type capacity) { elements_.reserve(capacity); }

  iterator begin() noexcept { return elements_.begin(); }
  iterator end() noexcept { return elements_.end(); }
  const_iterator begin() const noexcept { return elements_.begin(); }
  const_iterator end() const noexcept { return elements_.end(); }

  template <typename... Args>
  Index emplace_back(Args&&... args) {
    const Index index{static_cast<typename Index::rep>(elements_.size())};
    elements_.emplace_back(std::forward<Args>(args)...);
    return index;
  }

 private:
  std::vector<T> elements_;
};

}  // namespace nlib
