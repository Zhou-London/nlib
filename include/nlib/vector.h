#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace nq {

// std::vector with the interface trimmed to the hot paths. Deviations:
//  - push_back/emplace_back never check capacity: the caller must ensure
//    size() < capacity() first, via reserve() or the inline buffer. No at();
//    no allocator or pmr support.
//  - Small-buffer optimization: up to `inline_capacity` elements live inside
//    the object, so short vectors never allocate. While the inline buffer is
//    active, move construction/assignment and swap move elements one by one
//    (O(size())) and invalidate iterators into the source.
// The remaining members (insert, resize, assign, reserve) grow the buffer as
// in std::vector, geometrically. Reallocation moves elements with
// move_if_noexcept, so reserve keeps the strong exception guarantee.
template <typename T>
class vector {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  vector() noexcept = default;

  explicit vector(size_type count) : vector() { resize(count); }

  vector(size_type count, const T& value) : vector() { assign(count, value); }

  template <std::forward_iterator It>
  vector(It first, It last) : vector() {
    assign(first, last);
  }

  vector(std::initializer_list<T> il) : vector() { assign(il.begin(), il.end()); }

  vector(const vector& other) : vector() { assign(other.begin(), other.end()); }

  vector(vector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (other.is_inline()) {
      relocate(inline_data(), other.data_, other.size_);
      size_ = other.size_;
    } else {
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
    }
    other.reset_to_inline();
  }

  vector& operator=(const vector& other) {
    if (this != &other) assign(other.begin(), other.end());
    return *this;
  }

  vector& operator=(vector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (this == &other) return *this;
    std::destroy_n(data_, size_);
    if (!is_inline()) deallocate(data_);
    if (other.is_inline()) {
      data_ = inline_data();
      size_ = 0;
      capacity_ = inline_capacity;
      relocate(inline_data(), other.data_, other.size_);
      size_ = other.size_;
    } else {
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
    }
    other.reset_to_inline();
    return *this;
  }

  vector& operator=(std::initializer_list<T> il) {
    assign(il.begin(), il.end());
    return *this;
  }

  ~vector() {
    std::destroy_n(data_, size_);
    if (!is_inline()) deallocate(data_);
  }

  void assign(size_type count, const T& value) {
    clear();
    reserve(count);
    for (; size_ < count; ++size_) ::new (static_cast<void*>(data_ + size_)) T(value);
  }

  template <std::forward_iterator It>
  void assign(It first, It last) {
    clear();
    reserve(static_cast<size_type>(std::distance(first, last)));
    for (; first != last; ++first, ++size_)
      ::new (static_cast<void*>(data_ + size_)) T(*first);
  }

  // ---- element access ----

  reference operator[](size_type i) noexcept { return data_[i]; }
  const_reference operator[](size_type i) const noexcept { return data_[i]; }
  reference front() noexcept { return data_[0]; }
  const_reference front() const noexcept { return data_[0]; }
  reference back() noexcept { return data_[size_ - 1]; }
  const_reference back() const noexcept { return data_[size_ - 1]; }
  pointer data() noexcept { return data_; }
  const_pointer data() const noexcept { return data_; }

  // ---- iterators ----

  iterator begin() noexcept { return data_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator cbegin() const noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator end() const noexcept { return data_ + size_; }
  const_iterator cend() const noexcept { return data_ + size_; }
  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  // ---- capacity ----

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }

  void reserve(size_type n) {
    if (n > capacity_) reallocate(n);
  }

  // Returns storage to the inline buffer when size() fits there, otherwise
  // shrinks the heap buffer to size().
  void shrink_to_fit() {
    if (is_inline()) return;
    if (size_ <= inline_capacity) {
      T* heap = data_;
      const size_type heap_size = size_;
      relocate(inline_data(), heap, heap_size);
      deallocate(heap);
      data_ = inline_data();
      capacity_ = inline_capacity;
    } else if (size_ < capacity_) {
      reallocate(size_);
    }
  }

  // ---- modifiers ----

  void clear() noexcept {
    std::destroy_n(data_, size_);
    size_ = 0;
  }

  // Appends without checking capacity. Precondition: size() < capacity().
  void push_back(const T& value) {
    ::new (static_cast<void*>(data_ + size_)) T(value);
    ++size_;
  }
  void push_back(T&& value) {
    ::new (static_cast<void*>(data_ + size_)) T(std::move(value));
    ++size_;
  }
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    ::new (static_cast<void*>(data_ + size_)) T(std::forward<Args>(args)...);
    return data_[size_++];
  }

  // Precondition: !empty().
  void pop_back() { data_[--size_].~T(); }

  iterator insert(const_iterator pos, const T& value) { return emplace(pos, value); }
  iterator insert(const_iterator pos, T&& value) { return emplace(pos, std::move(value)); }

  iterator insert(const_iterator pos, size_type count, const T& value) {
    const size_type idx = index_of(pos);
    if (count == 0) return data_ + idx;
    const T tmp(value);  // survives reallocation and the rotate below
    ensure_room(count);
    const size_type old_size = size_;
    for (size_type i = 0; i < count; ++i, ++size_)
      ::new (static_cast<void*>(data_ + size_)) T(tmp);
    std::rotate(data_ + idx, data_ + old_size, data_ + size_);
    return data_ + idx;
  }

  template <std::forward_iterator It>
  iterator insert(const_iterator pos, It first, It last) {
    const size_type idx = index_of(pos);
    ensure_room(static_cast<size_type>(std::distance(first, last)));
    const size_type old_size = size_;
    for (; first != last; ++first, ++size_)
      ::new (static_cast<void*>(data_ + size_)) T(*first);
    std::rotate(data_ + idx, data_ + old_size, data_ + size_);
    return data_ + idx;
  }

  iterator insert(const_iterator pos, std::initializer_list<T> il) {
    return insert(pos, il.begin(), il.end());
  }

  template <typename... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    const size_type idx = index_of(pos);
    T tmp(std::forward<Args>(args)...);  // args may alias elements about to shift
    ensure_room(1);
    ::new (static_cast<void*>(data_ + size_)) T(std::move(tmp));
    ++size_;
    std::rotate(data_ + idx, data_ + size_ - 1, data_ + size_);
    return data_ + idx;
  }

  iterator erase(const_iterator pos) { return erase(pos, pos + 1); }

  iterator erase(const_iterator first, const_iterator last) {
    const size_type idx = index_of(first);
    const size_type count = static_cast<size_type>(last - first);
    std::move(data_ + idx + count, data_ + size_, data_ + idx);
    std::destroy_n(data_ + size_ - count, count);
    size_ -= count;
    return data_ + idx;
  }

  void resize(size_type n) {
    if (n <= size_) {
      std::destroy(data_ + n, data_ + size_);
      size_ = n;
      return;
    }
    if (n > capacity_) reallocate(grow_target(n));
    for (; size_ < n; ++size_) ::new (static_cast<void*>(data_ + size_)) T();
  }

  void resize(size_type n, const T& value) {
    if (n <= size_) {
      std::destroy(data_ + n, data_ + size_);
      size_ = n;
      return;
    }
    insert(end(), n - size_, value);
  }

  void swap(vector& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
    vector tmp(std::move(other));
    other = std::move(*this);
    *this = std::move(tmp);
  }

 private:
  // Sized so sizeof(vector) == 64 for small T; never below one element.
  static constexpr size_type inline_capacity =
      (64 - 3 * sizeof(void*)) / sizeof(T) > 1 ? (64 - 3 * sizeof(void*)) / sizeof(T) : 1;

  static T* allocate(size_type n) {
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{alignof(T)}));
  }

  static void deallocate(T* p) noexcept {
    ::operator delete(p, std::align_val_t{alignof(T)});
  }

  // Moves `n` elements from `src` into the uninitialized `dst` and destroys
  // the sources; if a move throws, `dst` is cleaned up and `src` is intact.
  static void relocate(T* dst, T* src, size_type n) {
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(dst, src, n * sizeof(T));
    } else {
      size_type i = 0;
      try {
        for (; i < n; ++i)
          ::new (static_cast<void*>(dst + i)) T(std::move_if_noexcept(src[i]));
      } catch (...) {
        while (i > 0) dst[--i].~T();
        throw;
      }
      std::destroy_n(src, n);
    }
  }

  T* inline_data() noexcept { return reinterpret_cast<T*>(sbo_); }
  bool is_inline() const noexcept { return data_ == reinterpret_cast<const T*>(sbo_); }

  size_type index_of(const_iterator pos) const noexcept {
    return static_cast<size_type>(pos - data_);
  }

  size_type grow_target(size_type needed) const noexcept {
    const size_type doubled = capacity_ * 2;
    return doubled > needed ? doubled : needed;
  }

  void ensure_room(size_type extra) {
    if (capacity_ - size_ < extra) reallocate(grow_target(size_ + extra));
  }

  void reallocate(size_type new_cap) {
    T* new_data = allocate(new_cap);
    try {
      relocate(new_data, data_, size_);
    } catch (...) {
      deallocate(new_data);
      throw;
    }
    if (!is_inline()) deallocate(data_);
    data_ = new_data;
    capacity_ = new_cap;
  }

  void reset_to_inline() noexcept {
    data_ = inline_data();
    size_ = 0;
    capacity_ = inline_capacity;
  }

  T* data_ = inline_data();  // inline buffer while == inline_data(), heap otherwise
  size_type size_ = 0;
  size_type capacity_ = inline_capacity;
  alignas(T) std::byte sbo_[inline_capacity * sizeof(T)];
};

template <typename T>
bool operator==(const vector<T>& a, const vector<T>& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

template <typename T>
void swap(vector<T>& a, vector<T>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

}  // namespace nq
