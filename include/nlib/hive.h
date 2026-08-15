#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace nq {

// Minimal C++26 std::hive (P0447): unordered element pool with O(1) insert
// and erase.
//
// Guarantees, matching std::hive:
//  - element addresses are stable for their lifetime; insert/erase never move
//    existing elements;
//  - insert invalidates no iterators; erase invalidates only iterators to the
//    erased element;
//  - iteration order is unspecified.
// Elements enter only by move or in-place construction; not copyable.
// Thread-compatible: concurrent const access is safe; writes need external
// synchronization.
//
// Storage is a chain of geometrically growing blocks, each a single
// allocation holding header + slot array + skipfield. Erased slots are
// tracked as jump-counting runs:
//
//   skipfield[i] == 0                      -> slot i holds an element;
//   erased run [s, e] of length N          -> skipfield[s] == skipfield[e] == N.
//
// Interior run values are never read: every lookup lands on a run head, a run
// tail, or an occupied slot. Forward iteration only steps onto run heads, so
// `i += skipfield[i]` jumps a whole run in O(1); reverse iteration lands on
// run tails and uses `i -= skipfield[i]`. Erase splices and merges runs with
// O(1) skipfield writes.
//
// Each block keeps a doubly linked free list of erased runs ("skipblocks"),
// nodes stored in the element memory of each run's head slot; blocks that
// contain holes are chained globally. Insert takes, in order: the head slot
// of the first holey block's first skipblock, then the tail block's untouched
// region, then a new block.
template <typename T>
class hive {
  using skip_t = std::uint16_t;
  static constexpr skip_t npos = 0xFFFF;

  struct free_node {  // free-list node, lives in the head slot of a skipblock
    skip_t prev;
    skip_t next;
  };

  union slot_type {
    T value;
    free_node links;
    slot_type() noexcept {}
    ~slot_type() {}
  };

  struct block_type {
    block_type* next = nullptr;
    block_type* prev = nullptr;
    block_type* next_erased = nullptr;  // doubly linked chain of holey blocks
    block_type* prev_erased = nullptr;
    slot_type* slots = nullptr;
    skip_t* skipfield = nullptr;
    skip_t free_head = npos;  // head skipblock; != npos <=> block is on the erased chain
    std::size_t capacity = 0;
    std::size_t high_water = 0;  // start of the never-used tail region
    std::size_t active = 0;      // live elements; >= 1 for chained blocks (empty ones are freed)
  };

  template <bool Const>
  class iterator_impl {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<Const, const T*, T*>;
    using reference = std::conditional_t<Const, const T&, T&>;

    iterator_impl() noexcept = default;

    template <bool C>
      requires(Const && !C)
    iterator_impl(const iterator_impl<C>& other) noexcept
        : owner_(other.owner_), blk_(other.blk_), idx_(other.idx_) {}

    reference operator*() const noexcept { return blk_->slots[idx_].value; }
    pointer operator->() const noexcept { return std::addressof(blk_->slots[idx_].value); }

    iterator_impl& operator++() noexcept {
      ++idx_;
      if (idx_ < blk_->high_water) {
        idx_ += blk_->skipfield[idx_];  // lands on a run head or an occupied slot (0)
        if (idx_ < blk_->high_water) return *this;
      }
      for (blk_ = blk_->next; blk_; blk_ = blk_->next) {
        idx_ = blk_->skipfield[0];
        if (idx_ < blk_->high_water) return *this;
      }
      idx_ = 0;  // end()
      return *this;
    }

    iterator_impl operator++(int) noexcept {
      iterator_impl tmp = *this;
      ++*this;
      return tmp;
    }

    iterator_impl& operator--() noexcept {
      block_type* b = blk_;
      std::size_t i = idx_;
      if (!b) {  // --end()
        b = owner_->tail_;
        i = b->high_water;
      }
      for (;;) {
        if (i == 0) {  // --begin() is UB, as in the standard
          b = b->prev;
          i = b->high_water;
          continue;
        }
        --i;
        const std::size_t skip = b->skipfield[i];  // run tail or occupied slot
        if (skip == 0) break;
        if (skip > i) {  // run extends to the block start
          i = 0;
          continue;
        }
        i -= skip;  // tail - length = occupied slot just before the run
        break;
      }
      blk_ = b;
      idx_ = i;
      return *this;
    }

    iterator_impl operator--(int) noexcept {
      iterator_impl tmp = *this;
      --*this;
      return tmp;
    }

    bool operator==(const iterator_impl& other) const noexcept {
      return blk_ == other.blk_ && idx_ == other.idx_;
    }

   private:
    friend hive;
    template <bool C>
    friend class iterator_impl;

    iterator_impl(const hive* owner, block_type* blk, std::size_t idx) noexcept
        : owner_(owner), blk_(blk), idx_(idx) {}

    const hive* owner_ = nullptr;
    block_type* blk_ = nullptr;
    std::size_t idx_ = 0;
  };

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = iterator_impl<false>;
  using const_iterator = iterator_impl<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  hive() noexcept = default;

  hive(const hive&) = delete;
  hive& operator=(const hive&) = delete;

  hive(hive&& other) noexcept { steal(other); }

  hive& operator=(hive&& other) noexcept {
    if (this != &other) {
      clear();
      steal(other);
    }
    return *this;
  }

  ~hive() { clear(); }

  // ---- iterators ----

  iterator begin() noexcept {
    for (block_type* b = head_; b; b = b->next) {
      const std::size_t i = b->skipfield[0];
      if (i < b->high_water) return iterator(this, b, i);
    }
    return end();
  }
  const_iterator begin() const noexcept {
    for (block_type* b = head_; b; b = b->next) {
      const std::size_t i = b->skipfield[0];
      if (i < b->high_water) return const_iterator(this, b, i);
    }
    return end();
  }
  const_iterator cbegin() const noexcept { return begin(); }

  iterator end() noexcept { return iterator(this, nullptr, 0); }
  const_iterator end() const noexcept { return const_iterator(this, nullptr, 0); }
  const_iterator cend() const noexcept { return end(); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  // ---- capacity ----

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }
  size_type max_size() const noexcept { return static_cast<size_type>(-1) / sizeof(T); }

  // ---- modifiers ----

  iterator insert(T&& value) { return emplace(std::move(value)); }

  template <typename... Args>
  iterator emplace(Args&&... args) {
    if (erased_head_) {  // 1) reuse the head slot of the first holey block's head skipblock
      block_type* b = erased_head_;
      const std::size_t idx = b->free_head;
      slot_type* s = b->slots + idx;
      const free_node ln = s->links;  // construction overwrites the node; save it first
      const skip_t run = b->skipfield[idx];
      try {
        ::new (static_cast<void*>(std::addressof(s->value))) T(std::forward<Args>(args)...);
      } catch (...) {
        s->links = ln;
        throw;
      }
      b->skipfield[idx] = 0;
      if (run > 1) {  // run shrinks to [idx+1, idx+run-1]; free-list node moves with its head
        b->skipfield[idx + 1] = b->skipfield[idx + run - 1] = static_cast<skip_t>(run - 1);
        b->slots[idx + 1].links = ln;  // ln.prev == npos: idx was the list head
        if (ln.next != npos) b->slots[ln.next].links.prev = static_cast<skip_t>(idx + 1);
        b->free_head = static_cast<skip_t>(idx + 1);
      } else {  // skipblock exhausted; unlink it
        b->free_head = ln.next;
        if (ln.next != npos)
          b->slots[ln.next].links.prev = npos;
        else
          unlink_erased(b);
      }
      ++b->active;
      ++size_;
      return iterator(this, b, idx);
    }
    if (!tail_ || tail_->high_water == tail_->capacity) {  // 2) need a fresh block
      block_type* nb = allocate_block(next_block_capacity());
      nb->prev = tail_;
      if (tail_)
        tail_->next = nb;
      else
        head_ = nb;
      tail_ = nb;
    }
    block_type* b = tail_;  // 3) use the tail block's untouched region (skipfield pre-zeroed)
    const std::size_t idx = b->high_water;
    ::new (static_cast<void*>(std::addressof(b->slots[idx].value)))
        T(std::forward<Args>(args)...);
    ++b->high_water;
    ++b->active;
    ++size_;
    return iterator(this, b, idx);
  }

  iterator erase(const_iterator pos) {
    block_type* b = pos.blk_;
    const std::size_t idx = pos.idx_;
    iterator next(this, b, idx);
    ++next;  // the block may get freed below; compute the successor first
    b->slots[idx].value.~T();
    --b->active;
    --size_;
    if (b->active == 0) {  // block now empty: unlink from all chains and free it
      if (b->free_head != npos) unlink_erased(b);
      if (b->prev)
        b->prev->next = b->next;
      else
        head_ = b->next;
      if (b->next)
        b->next->prev = b->prev;
      else
        tail_ = b->prev;
      deallocate_block(b);
      return next;
    }
    // Skipfield update, four O(1) cases by neighbor state. Since idx held an
    // element, a nonzero left neighbor is necessarily a run tail and a nonzero
    // right neighbor a run head, so both reads are true run lengths.
    skip_t* const sf = b->skipfield;
    const skip_t L = (idx > 0) ? sf[idx - 1] : 0;
    const skip_t R = (idx + 1 < b->high_water) ? sf[idx + 1] : 0;
    if (L == 0 && R == 0) {  // isolated hole: new skipblock, push onto free list
      sf[idx] = 1;
      push_free(b, idx);
    } else if (L != 0 && R == 0) {  // extend left run: head unchanged, no list update
      sf[idx - L] = sf[idx] = static_cast<skip_t>(L + 1);
    } else if (L == 0 && R != 0) {  // extend right run: its head moves one slot left
      sf[idx] = sf[idx + R] = static_cast<skip_t>(R + 1);
      move_free_node(b, idx + 1, idx);
    } else {  // merge both: right skipblock leaves the list, left head takes over
      sf[idx - L] = sf[idx + R] = static_cast<skip_t>(L + R + 1);
      remove_free_node(b, idx + 1);
    }
    return next;
  }

  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) first = erase(first);
    return iterator(this, last.blk_, last.idx_);
  }

  // Destroys all elements and frees all storage.
  void clear() noexcept {
    block_type* b = head_;
    while (b) {
      block_type* next = b->next;
      for (std::size_t i = 0; i < b->high_water;) {
        const std::size_t skip = b->skipfield[i];
        if (skip == 0) {
          b->slots[i].value.~T();
          ++i;
        } else {
          i += skip;
        }
      }
      deallocate_block(b);
      b = next;
    }
    head_ = tail_ = erased_head_ = nullptr;
    size_ = 0;
  }

  void swap(hive& other) noexcept {
    std::swap(head_, other.head_);
    std::swap(tail_, other.tail_);
    std::swap(erased_head_, other.erased_head_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
  }

  // Returns an iterator to `*p`, or end() if `p` does not point at a live
  // element of this hive. O(number of blocks).
  iterator get_iterator(const_pointer p) noexcept {
    auto [b, idx] = locate(p);
    return b ? iterator(this, b, idx) : end();
  }
  const_iterator get_iterator(const_pointer p) const noexcept {
    auto [b, idx] = locate(p);
    return b ? const_iterator(this, b, idx) : end();
  }

 private:
  static constexpr std::size_t min_block_capacity = 8;
  static constexpr std::size_t max_block_capacity = 8192;
  static_assert(max_block_capacity < npos);  // skipfield counts and list indices fit skip_t
  static constexpr std::size_t block_align =
      alignof(block_type) > alignof(slot_type) ? alignof(block_type) : alignof(slot_type);

  // Each new block doubles total capacity, clamped to [min, max].
  std::size_t next_block_capacity() const noexcept {
    if (capacity_ < min_block_capacity) return min_block_capacity;
    return capacity_ > max_block_capacity ? max_block_capacity : capacity_;
  }

  // Header, slot array and skipfield share one allocation.
  block_type* allocate_block(std::size_t cap) {
    constexpr std::size_t off =
        (sizeof(block_type) + alignof(slot_type) - 1) / alignof(slot_type) * alignof(slot_type);
    void* raw = ::operator new(off + cap * (sizeof(slot_type) + sizeof(skip_t)),
                               std::align_val_t{block_align});
    auto* b = ::new (raw) block_type{};
    b->slots = reinterpret_cast<slot_type*>(static_cast<char*>(raw) + off);
    b->skipfield = reinterpret_cast<skip_t*>(b->slots + cap);
    std::memset(b->skipfield, 0, cap * sizeof(skip_t));
    b->capacity = cap;
    capacity_ += cap;
    return b;
  }

  void deallocate_block(block_type* b) noexcept {
    capacity_ -= b->capacity;
    b->~block_type();
    ::operator delete(b, std::align_val_t{block_align});
  }

  // ---- per-block skipblock free list ----

  void push_free(block_type* b, std::size_t idx) noexcept {
    slot_type* s = b->slots + idx;
    ::new (static_cast<void*>(s)) slot_type;
    s->links = free_node{npos, b->free_head};
    if (b->free_head != npos)
      b->slots[b->free_head].links.prev = static_cast<skip_t>(idx);
    else
      link_erased(b);
    b->free_head = static_cast<skip_t>(idx);
  }

  void move_free_node(block_type* b, std::size_t from, std::size_t to) noexcept {
    const free_node ln = b->slots[from].links;
    slot_type* s = b->slots + to;
    ::new (static_cast<void*>(s)) slot_type;
    s->links = ln;
    if (ln.prev != npos)
      b->slots[ln.prev].links.next = static_cast<skip_t>(to);
    else
      b->free_head = static_cast<skip_t>(to);
    if (ln.next != npos) b->slots[ln.next].links.prev = static_cast<skip_t>(to);
  }

  void remove_free_node(block_type* b, std::size_t idx) noexcept {
    const free_node ln = b->slots[idx].links;
    if (ln.prev != npos)
      b->slots[ln.prev].links.next = ln.next;
    else
      b->free_head = ln.next;
    if (ln.next != npos) b->slots[ln.next].links.prev = ln.prev;
  }

  // ---- global chain of holey blocks ----

  void link_erased(block_type* b) noexcept {
    b->next_erased = erased_head_;
    if (erased_head_) erased_head_->prev_erased = b;
    erased_head_ = b;
  }

  void unlink_erased(block_type* b) noexcept {
    if (b->prev_erased)
      b->prev_erased->next_erased = b->next_erased;
    else
      erased_head_ = b->next_erased;
    if (b->next_erased) b->next_erased->prev_erased = b->prev_erased;
    b->next_erased = b->prev_erased = nullptr;
  }

  // Maps `p` to its (block, slot index); {nullptr, 0} when `p` is not a live
  // element.
  std::pair<block_type*, std::size_t> locate(const_pointer p) const noexcept {
    const auto addr = reinterpret_cast<std::uintptr_t>(p);
    for (block_type* b = head_; b; b = b->next) {
      const auto lo = reinterpret_cast<std::uintptr_t>(b->slots);
      const auto hi = reinterpret_cast<std::uintptr_t>(b->slots + b->high_water);
      if (addr >= lo && addr < hi) {
        const auto idx = static_cast<std::size_t>(reinterpret_cast<const slot_type*>(p) - b->slots);
        if (b->skipfield[idx] == 0) return {b, idx};
        return {nullptr, 0};
      }
    }
    return {nullptr, 0};
  }

  void steal(hive& other) noexcept {
    head_ = std::exchange(other.head_, nullptr);
    tail_ = std::exchange(other.tail_, nullptr);
    erased_head_ = std::exchange(other.erased_head_, nullptr);
    size_ = std::exchange(other.size_, 0);
    capacity_ = std::exchange(other.capacity_, 0);
  }

  block_type* head_ = nullptr;
  block_type* tail_ = nullptr;
  block_type* erased_head_ = nullptr;  // blocks containing holes
  size_type size_ = 0;
  size_type capacity_ = 0;
};

template <typename T>
void swap(hive<T>& a, hive<T>& b) noexcept {
  a.swap(b);
}

}  // namespace nq
