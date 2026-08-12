#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace nq {

// Growable object pool. emplace() constructs an element and returns an index
// handle; release() destroys the element and recycles its slot, most recently
// released first. Handles stay valid until released. Storage is a single
// std::vector, so growth may move elements: emplace() invalidates references,
// never handles. Not copyable or movable. Thread-compatible: concurrent const
// access is safe; writes need external synchronization.
template <typename T>
class pool {
  // Holds either a live element or a free-list link; `live` discriminates so
  // relocation and destruction work for any move-constructible T.
  struct slot_type {
    union {
      std::size_t next_free;  // next slot in the free list; valid while !live
      T value;
    };
    bool live;

    slot_type() noexcept : next_free(0), live(false) {}
    slot_type(slot_type&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : live(other.live) {
      if (live)
        ::new (static_cast<void*>(std::addressof(value))) T(std::move(other.value));
      else
        next_free = other.next_free;
    }
    ~slot_type() {
      if (live) value.~T();
    }
  };

 public:
  using value_type = T;
  using size_type = std::size_t;

  // Preallocates storage for `capacity` elements; the pool starts empty.
  explicit pool(size_type capacity) { slots_.reserve(capacity); }

  pool(const pool&) = delete;
  pool& operator=(const pool&) = delete;

  // Precondition: `handle` refers to a live element.
  T& operator[](size_type handle) noexcept { return slots_[handle].value; }
  const T& operator[](size_type handle) const noexcept { return slots_[handle].value; }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return slots_.capacity(); }

  // Constructs an element and returns its handle. If construction throws, no
  // element is added.
  template <typename... Args>
  size_type emplace(Args&&... args) {
    if (free_head_ != npos) {
      const size_type handle = free_head_;
      slot_type& s = slots_[handle];
      const size_type next = s.next_free;  // constructing `value` clobbers the union
      try {
        ::new (static_cast<void*>(std::addressof(s.value))) T(std::forward<Args>(args)...);
      } catch (...) {
        s.next_free = next;
        throw;
      }
      s.live = true;
      free_head_ = next;
      ++size_;
      return handle;
    }
    slots_.resize(slots_.size() + 1);
    slot_type& s = slots_.back();
    try {
      ::new (static_cast<void*>(std::addressof(s.value))) T(std::forward<Args>(args)...);
    } catch (...) {
      slots_.pop_back();
      throw;
    }
    s.live = true;
    ++size_;
    return slots_.size() - 1;
  }

  // Precondition: `handle` refers to a live element.
  void release(size_type handle) noexcept {
    slot_type& s = slots_[handle];
    s.value.~T();
    s.live = false;
    s.next_free = free_head_;
    free_head_ = handle;
    --size_;
  }

 private:
  static constexpr size_type npos = static_cast<size_type>(-1);

  std::vector<slot_type> slots_;
  size_type free_head_ = npos;  // top of the free-slot stack; npos when empty
  size_type size_ = 0;          // live elements
};

}  // namespace nq
