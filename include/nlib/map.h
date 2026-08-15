#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace nq {

// Open-addressing hash map in one flat allocation: a std::pair<Key, T> slot
// array plus one control byte per slot (empty, tombstone, or a 7-bit hash
// fragment that pre-filters key comparisons). Capacity is a power of two,
// probing is linear, and the table rehashes at 3/4 load. Hash output is mixed
// internally, so an identity std::hash is fine.
//
// value_type is std::pair<Key, T>, not pair<const Key, T>, so slots can
// relocate on rehash; writing `first` through an iterator corrupts the table.
// Insert and rehash invalidate all iterators and references; erase invalidates
// only the erased element. Iteration order is unspecified. Elements enter only
// by move or in-place construction; not copyable. Thread-compatible:
// concurrent const access is safe; writes need external synchronization.
template <typename Key, typename T, typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class map {
  // Control byte values. Full slots hold their key's 7-bit hash fragment, so
  // the high bit distinguishes full (clear) from empty/tombstone (set).
  static constexpr std::uint8_t ctrl_empty = 0x80;
  static constexpr std::uint8_t ctrl_deleted = 0x81;

  template <bool Const>
  class iterator_impl {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::pair<Key, T>;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<Const, const value_type*, value_type*>;
    using reference = std::conditional_t<Const, const value_type&, value_type&>;

    iterator_impl() noexcept = default;

    template <bool C>
      requires(Const && !C)
    iterator_impl(const iterator_impl<C>& other) noexcept
        : ctrl_(other.ctrl_), slot_(other.slot_) {}

    reference operator*() const noexcept { return *slot_; }
    pointer operator->() const noexcept { return slot_; }

    iterator_impl& operator++() noexcept {
      ++ctrl_;
      ++slot_;
      skip();
      return *this;
    }

    iterator_impl operator++(int) noexcept {
      iterator_impl tmp = *this;
      ++*this;
      return tmp;
    }

    bool operator==(const iterator_impl& other) const noexcept = default;

   private:
    friend map;
    template <bool C>
    friend class iterator_impl;

    iterator_impl(std::uint8_t* ctrl, value_type* slot) noexcept
        : ctrl_(ctrl), slot_(slot) {}

    // Advances to the next full slot; the sentinel byte reads as full, so the
    // scan cannot run past the table.
    void skip() noexcept {
      while (*ctrl_ & 0x80) {
        ++ctrl_;
        ++slot_;
      }
    }

    std::uint8_t* ctrl_ = nullptr;
    std::pair<Key, T>* slot_ = nullptr;
  };

 public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = std::pair<Key, T>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using iterator = iterator_impl<false>;
  using const_iterator = iterator_impl<true>;

  map() noexcept = default;

  map(const map&) = delete;
  map& operator=(const map&) = delete;

  map(map&& other) noexcept { steal(other); }

  map& operator=(map&& other) noexcept {
    if (this != &other) {
      clear();
      steal(other);
    }
    return *this;
  }

  ~map() { clear(); }

  // ---- iterators ----

  iterator begin() noexcept {
    if (size_ == 0) return end();
    iterator it(ctrl_, slots_);
    it.skip();
    return it;
  }
  const_iterator begin() const noexcept {
    if (size_ == 0) return end();
    const_iterator it(ctrl_, slots_);
    it.skip();
    return it;
  }
  const_iterator cbegin() const noexcept { return begin(); }

  iterator end() noexcept { return iterator(ctrl_ + capacity_, slots_ + capacity_); }
  const_iterator end() const noexcept {
    return const_iterator(ctrl_ + capacity_, slots_ + capacity_);
  }
  const_iterator cend() const noexcept { return end(); }

  // ---- capacity ----

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type max_size() const noexcept { return static_cast<size_type>(-1) / sizeof(value_type); }

  // Guarantees that the next `n - size()` insertions do not rehash.
  void reserve(size_type n) {
    if (n < size_) n = size_;
    if (n + (used_ - size_) <= max_load()) return;
    rehash_to(capacity_for(n));
  }

  // ---- lookup ----

  iterator find(const Key& key) noexcept {
    const size_type idx = find_index(key);
    return idx != npos ? make_iter(idx) : end();
  }
  const_iterator find(const Key& key) const noexcept {
    const size_type idx = find_index(key);
    return idx != npos ? const_iterator(ctrl_ + idx, slots_ + idx) : end();
  }

  bool contains(const Key& key) const noexcept { return find_index(key) != npos; }

  // Throws std::out_of_range if `key` is absent.
  T& at(const Key& key) {
    const size_type idx = find_index(key);
    if (idx == npos) throw std::out_of_range("nq::map::at");
    return slots_[idx].second;
  }
  const T& at(const Key& key) const {
    const size_type idx = find_index(key);
    if (idx == npos) throw std::out_of_range("nq::map::at");
    return slots_[idx].second;
  }

  // ---- modifiers ----

  // Returns the element with `key` and whether an insertion happened. When
  // `key` is already present, `value` is left unmoved.
  std::pair<iterator, bool> insert(value_type&& value) {
    return insert_key(value.first, [&](value_type* slot) {
      ::new (static_cast<void*>(slot)) value_type(std::move(value));
    });
  }

  // Constructs a value_type from `args`, then inserts it by move; the
  // temporary is discarded when the key is already present.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return insert(value_type(std::forward<Args>(args)...));
  }

  // Constructs the mapped value from `args` only on insertion; `key` and
  // `args` are left unmoved when the key is already present.
  template <typename... Args>
  std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
    return insert_key(key, [&](value_type* slot) {
      ::new (static_cast<void*>(slot))
          value_type(std::piecewise_construct, std::forward_as_tuple(std::move(key)),
                     std::forward_as_tuple(std::forward<Args>(args)...));
    });
  }

  // Inserts a value-initialized mapped value when `key` is absent.
  T& operator[](Key&& key) { return try_emplace(std::move(key)).first->second; }

  iterator erase(const_iterator pos) noexcept {
    const size_type idx = static_cast<size_type>(pos.ctrl_ - ctrl_);
    slots_[idx].~value_type();
    --size_;
    // Reverting to empty is safe iff no probe chain continues past this slot.
    // Chains advance one slot at a time and never cross an empty slot, so an
    // empty right neighbor proves none continues past idx.
    if (ctrl_[(idx + 1) & (capacity_ - 1)] == ctrl_empty) {
      ctrl_[idx] = ctrl_empty;
      --used_;
    } else {
      ctrl_[idx] = ctrl_deleted;
    }
    iterator next(ctrl_ + idx, slots_ + idx);
    next.skip();
    return next;
  }

  size_type erase(const Key& key) noexcept {
    const size_type idx = find_index(key);
    if (idx == npos) return 0;
    erase(const_iterator(ctrl_ + idx, slots_ + idx));
    return 1;
  }

  // Destroys all elements and frees all storage.
  void clear() noexcept {
    for (size_type i = 0; i < capacity_; ++i)
      if (!(ctrl_[i] & 0x80)) slots_[i].~value_type();
    if (capacity_ != 0)
      ::operator delete(slots_, std::align_val_t{alignof(value_type)});
    slots_ = nullptr;
    ctrl_ = nullptr;
    capacity_ = 0;
    size_ = 0;
    used_ = 0;
  }

  void swap(map& other) noexcept {
    std::swap(slots_, other.slots_);
    std::swap(ctrl_, other.ctrl_);
    std::swap(capacity_, other.capacity_);
    std::swap(size_, other.size_);
    std::swap(used_, other.used_);
    std::swap(hash_, other.hash_);
    std::swap(eq_, other.eq_);
  }

 private:
  static constexpr size_type min_capacity = 8;
  static constexpr size_type npos = static_cast<size_type>(-1);

  // murmur3 finalizer. Low bits pick the bucket, the top 7 bits the control
  // fragment, so the two are decorrelated even for an identity std::hash.
  static std::uint64_t mix(std::size_t h) noexcept {
    std::uint64_t x = h;
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCD;
    x ^= x >> 33;
    x *= 0xC4CEB9FE1A85EC53;
    x ^= x >> 33;
    return x;
  }

  static std::uint8_t fragment(std::uint64_t h) noexcept {
    return static_cast<std::uint8_t>(h >> 57);
  }

  // Occupancy limit for full + tombstone slots. Keeping it below capacity
  // guarantees every probe terminates at an empty slot.
  size_type max_load() const noexcept { return capacity_ - capacity_ / 4; }

  // Smallest valid capacity whose load limit admits `n` elements.
  static size_type capacity_for(size_type n) noexcept {
    const size_type needed = std::bit_ceil((4 * n + 2) / 3);
    return needed > min_capacity ? needed : min_capacity;
  }

  iterator make_iter(size_type idx) noexcept { return iterator(ctrl_ + idx, slots_ + idx); }

  // Precondition: capacity_ != 0. Returns the index of `key`, or npos with
  // *slot set to the best insertion slot on the probe path: the first
  // tombstone if any, else the terminating empty slot.
  size_type locate(const Key& key, std::uint64_t h, std::uint8_t h2,
                   size_type* slot) const noexcept {
    const size_type mask = capacity_ - 1;
    size_type idx = static_cast<size_type>(h) & mask;
    size_type tomb = npos;
    for (;;) {
      const std::uint8_t c = ctrl_[idx];
      if (c == h2 && eq_(slots_[idx].first, key)) return idx;
      if (c == ctrl_empty) {
        *slot = tomb != npos ? tomb : idx;
        return npos;
      }
      if (c == ctrl_deleted && tomb == npos) tomb = idx;
      idx = (idx + 1) & mask;
    }
  }

  size_type find_index(const Key& key) const noexcept {
    if (size_ == 0) return npos;
    const std::uint64_t h = mix(hash_(key));
    size_type unused;
    return locate(key, h, fragment(h), &unused);
  }

  // Precondition: the table has no tombstones on the probe path of `h` (fresh
  // after rehash_to). Returns the first empty slot.
  size_type free_slot(std::uint64_t h) const noexcept {
    const size_type mask = capacity_ - 1;
    size_type idx = static_cast<size_type>(h) & mask;
    while (ctrl_[idx] != ctrl_empty) idx = (idx + 1) & mask;
    return idx;
  }

  // Shared insert path: probes for `key` and, when absent, calls
  // `construct(slot)` to build the element in place. If `construct` throws,
  // the map is unchanged.
  template <typename Construct>
  std::pair<iterator, bool> insert_key(const Key& key, Construct construct) {
    const std::uint64_t h = mix(hash_(key));
    const std::uint8_t h2 = fragment(h);
    size_type slot = 0;
    bool into_empty = true;
    if (capacity_ != 0) {
      const size_type idx = locate(key, h, h2, &slot);
      if (idx != npos) return {make_iter(idx), false};
      into_empty = ctrl_[slot] == ctrl_empty;
    }
    if (into_empty && used_ + 1 > max_load()) {
      // Tombstone-heavy tables rehash in place; otherwise capacity doubles.
      rehash_to(capacity_ == 0             ? min_capacity
                : size_ >= capacity_ / 2   ? capacity_ * 2
                                           : capacity_);
      slot = free_slot(h);
    }
    construct(slots_ + slot);
    ctrl_[slot] = h2;
    used_ += into_empty;
    ++size_;
    return {make_iter(slot), true};
  }

  // Slots and control bytes share one allocation: `cap` pairs, then `cap`
  // control bytes, then one sentinel byte that reads as full to stop
  // iteration.
  void allocate(size_type cap) {
    void* raw = ::operator new(cap * sizeof(value_type) + cap + 1,
                               std::align_val_t{alignof(value_type)});
    slots_ = static_cast<value_type*>(raw);
    ctrl_ = reinterpret_cast<std::uint8_t*>(slots_ + cap);
    std::memset(ctrl_, ctrl_empty, cap);
    ctrl_[cap] = 0;
    capacity_ = cap;
  }

  void rehash_to(size_type new_cap) {
    std::uint8_t* old_ctrl = ctrl_;
    value_type* old_slots = slots_;
    const size_type old_cap = capacity_;
    allocate(new_cap);
    used_ = size_;
    for (size_type i = 0; i < old_cap; ++i) {
      if (old_ctrl[i] & 0x80) continue;
      const std::uint64_t h = mix(hash_(old_slots[i].first));
      const size_type idx = free_slot(h);
      ::new (static_cast<void*>(slots_ + idx)) value_type(std::move(old_slots[i]));
      ctrl_[idx] = fragment(h);
      old_slots[i].~value_type();
    }
    if (old_cap != 0)
      ::operator delete(old_slots, std::align_val_t{alignof(value_type)});
  }

  void steal(map& other) noexcept {
    slots_ = std::exchange(other.slots_, nullptr);
    ctrl_ = std::exchange(other.ctrl_, nullptr);
    capacity_ = std::exchange(other.capacity_, 0);
    size_ = std::exchange(other.size_, 0);
    used_ = std::exchange(other.used_, 0);
    hash_ = std::move(other.hash_);
    eq_ = std::move(other.eq_);
  }

  value_type* slots_ = nullptr;
  std::uint8_t* ctrl_ = nullptr;
  size_type capacity_ = 0;  // slot count; a power of two, or 0 before first insert
  size_type size_ = 0;      // live elements
  size_type used_ = 0;      // full + tombstone slots; falls back to size_ on rehash
  [[no_unique_address]] Hash hash_;
  [[no_unique_address]] KeyEqual eq_;
};

template <typename Key, typename T, typename Hash, typename KeyEqual>
void swap(map<Key, T, Hash, KeyEqual>& a, map<Key, T, Hash, KeyEqual>& b) noexcept {
  a.swap(b);
}

}  // namespace nq
