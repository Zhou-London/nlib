#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <utility>

namespace nlib {

// Bounded single-producer single-consumer FIFO. Storage is one heap array
// sized at construction (the requested capacity rounds up to a power of two);
// the queue never grows, so a push to a full queue fails and the value stays
// with the caller.
//
// Thread contract: at most one thread calls the producer side (try_push,
// try_emplace) and at most one the consumer side (try_pop), concurrently and
// without blocking each other. size() and empty() are safe from either thread
// but return a snapshot that may be stale on arrival. Destruction must not
// race with any other call.
//
// `head_` and `tail_` are free-running counters masked into the slot array.
// Element hand-off uses release stores paired with acquire loads: the
// consumer observes a fully constructed element, and the producer observes a
// fully destroyed slot before reusing it. Each side owns one cache line
// holding its counter plus a cached copy of the other side's counter, so the
// hot path touches no shared line until the queue looks full or empty.
template <typename T>
class single_queue {
 public:
  using value_type = T;
  using size_type = std::size_t;

  // Allocates storage for `min_capacity` elements rounded up to a power of
  // two, minimum 1. Element construction happens per push.
  explicit single_queue(size_type min_capacity)
      : mask_(std::bit_ceil(min_capacity > 1 ? min_capacity : 1) - 1),
        slots_(static_cast<T*>(::operator new((mask_ + 1) * sizeof(T),
                                              std::align_val_t{alignof(T)}))) {}

  single_queue(const single_queue&) = delete;
  single_queue& operator=(const single_queue&) = delete;

  ~single_queue() {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    for (std::uint64_t i = head_.load(std::memory_order_relaxed); i != tail; ++i)
      slots_[i & mask_].~T();
    ::operator delete(slots_, std::align_val_t{alignof(T)});
  }

  // Constructs an element at the back; returns false without constructing
  // anything if the queue is full. Producer thread only.
  template <typename... Args>
  bool try_emplace(Args&&... args) {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - cached_head_ > mask_) {  // full against the snapshot; refresh it
      cached_head_ = head_.load(std::memory_order_acquire);  // consumer's slot release
      if (tail - cached_head_ > mask_) return false;
    }
    ::new (static_cast<void*>(slots_ + (tail & mask_))) T(std::forward<Args>(args)...);
    tail_.store(tail + 1, std::memory_order_release);  // publishes the element
    return true;
  }

  // On failure the argument is left unmoved.
  bool try_push(T&& value) { return try_emplace(std::move(value)); }

  // Removes and returns the front element, or std::nullopt if the queue is
  // empty. Consumer thread only.
  std::optional<T> try_pop() {
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    if (head == cached_tail_) {  // empty against the snapshot; refresh it
      cached_tail_ = tail_.load(std::memory_order_acquire);  // producer's publish
      if (head == cached_tail_) return std::nullopt;
    }
    T& slot = slots_[head & mask_];
    std::optional<T> out(std::move(slot));
    slot.~T();
    head_.store(head + 1, std::memory_order_release);  // releases the slot for reuse
    return out;
  }

  // Actual capacity: `min_capacity` from construction rounded up to a power
  // of two.
  size_type capacity() const noexcept { return mask_ + 1; }

  size_type size() const noexcept {
    // Loading `head_` first keeps the difference non-negative: `tail_` read
    // afterwards is at least the head value read here.
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    return static_cast<size_type>(tail_.load(std::memory_order_acquire) - head);
  }

  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

 private:
#ifdef __cpp_lib_hardware_interference_size
  static constexpr std::size_t cache_line = std::hardware_destructive_interference_size;
#else
  static constexpr std::size_t cache_line = 64;
#endif

  const std::uint64_t mask_;  // slot count - 1; slot index is `counter & mask_`
  T* const slots_;

  // Consumer-owned cache line.
  alignas(cache_line) std::atomic<std::uint64_t> head_{0};  // counter of the next element to pop
  std::uint64_t cached_tail_ = 0;  // consumer's snapshot of tail_

  // Producer-owned cache line.
  alignas(cache_line) std::atomic<std::uint64_t> tail_{0};  // counter of the next slot to fill
  std::uint64_t cached_head_ = 0;  // producer's snapshot of head_
};

}  // namespace nlib
