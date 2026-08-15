#pragma once

#include <cstddef>
#include <cstring>
#include <new>

namespace nlib {

// Fixed-capacity fixed-size-block allocator over one contiguous buffer.
// allocate() hands out uninitialized blocks of block_size() bytes, most
// recently freed first, and returns nullptr once all blocks are outstanding;
// deallocate() makes a block available again. The destructor frees the
// buffer; outstanding blocks need not be deallocated first. Not copyable or
// movable. Thread-compatible: concurrent const access is safe; writes need
// external synchronization.
class memory_pool {
 public:
  using size_type = std::size_t;

  // Allocates a buffer of `capacity` blocks. `block_size` is rounded up to a
  // multiple of `alignment` and to at least sizeof(void*), where the free
  // list lives; `alignment` must be a power of two.
  memory_pool(size_type block_size, size_type capacity,
              size_type alignment = alignof(std::max_align_t))
      : align_(alignment > alignof(void*) ? alignment : alignof(void*)),
        block_size_(round_up(block_size > sizeof(void*) ? block_size : sizeof(void*), align_)),
        capacity_(capacity),
        data_(static_cast<std::byte*>(
            ::operator new(capacity * block_size_, std::align_val_t{align_}))) {}

  memory_pool(const memory_pool&) = delete;
  memory_pool& operator=(const memory_pool&) = delete;

  ~memory_pool() { ::operator delete(data_, std::align_val_t{align_}); }

  size_type block_size() const noexcept { return block_size_; }
  size_type alignment() const noexcept { return align_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }

  // Returns a block of block_size() bytes aligned to alignment(), or nullptr
  // when the pool is exhausted.
  void* allocate() noexcept {
    if (free_head_) {
      void* p = free_head_;
      std::memcpy(&free_head_, p, sizeof(void*));
      ++size_;
      return p;
    }
    if (used_ == capacity_) return nullptr;
    ++size_;
    return data_ + used_++ * block_size_;
  }

  // Precondition: `p` came from this pool's allocate() and is not already
  // deallocated.
  void deallocate(void* p) noexcept {
    std::memcpy(p, &free_head_, sizeof(void*));
    free_head_ = p;
    --size_;
  }

 private:
  static constexpr size_type round_up(size_type n, size_type a) noexcept {
    return (n + a - 1) / a * a;
  }

  const size_type align_;
  const size_type block_size_;
  const size_type capacity_;
  std::byte* const data_;
  void* free_head_ = nullptr;  // LIFO free list threaded through freed blocks
  size_type used_ = 0;         // blocks ever handed out from the buffer's tail
  size_type size_ = 0;         // outstanding blocks
};

}  // namespace nlib
