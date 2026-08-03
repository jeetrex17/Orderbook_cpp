#pragma once

#include "Types.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// An open-addressing hash map from OrderId to a slab slot index.
//
// std::unordered_map allocates a node per entry and chases a pointer per
// lookup. Once the order slab removed the per-order std::list allocation, this
// map was the largest remaining source of malloc traffic in the profile. Open
// addressing keeps every entry in one contiguous array: no allocation per
// insert, and a hit usually touches a single cache line.
//
// Erasure uses backward-shift deletion rather than tombstones. Orders are
// erased constantly -- on every full fill and every cancel -- so tombstones
// would accumulate and force repeated rehashing just to clear them.
class IdIndex {
public:
  static constexpr uint32_t kMissing = std::numeric_limits<uint32_t>::max();

  IdIndex() { rehash(kInitialCapacity); }

  std::size_t size() const { return size_; }

  uint32_t find(OrderId key) const {
    // The load factor is capped below 1, so there is always an empty slot and
    // this loop always terminates.
    for (std::size_t i = home(key);; i = (i + 1) & mask_) {
      if (!slots_[i].occupied) {
        return kMissing;
      }
      if (slots_[i].key == key) {
        return slots_[i].value;
      }
    }
  }

  void insert(OrderId key, uint32_t value) {
    if ((size_ + 1) * 10 >= slots_.size() * kMaxLoadTenths) {
      rehash(slots_.size() * 2);
    }
    for (std::size_t i = home(key);; i = (i + 1) & mask_) {
      if (!slots_[i].occupied) {
        slots_[i] = Slot{key, value, true};
        ++size_;
        return;
      }
      if (slots_[i].key == key) {
        slots_[i].value = value;
        return;
      }
    }
  }

  bool erase(OrderId key) {
    std::size_t hole = home(key);
    for (;; hole = (hole + 1) & mask_) {
      if (!slots_[hole].occupied) {
        return false;
      }
      if (slots_[hole].key == key) {
        break;
      }
    }

    slots_[hole].occupied = false;
    --size_;

    // Walk forward from the hole. Any entry that probed *past* the hole to
    // reach its current slot must be shifted back into it, or a later lookup
    // would stop at the hole and wrongly conclude the key is absent.
    for (std::size_t j = (hole + 1) & mask_; slots_[j].occupied;
         j = (j + 1) & mask_) {
      const std::size_t k = home(slots_[j].key);

      // Leave slots_[j] where it is if its home lies cyclically within
      // (hole, j]: moving it back would place it *before* its home, where a
      // probe starting at k would never look.
      const bool home_between = (hole <= j) ? (hole < k && k <= j)
                                            : (hole < k || k <= j);
      if (home_between) {
        continue;
      }

      slots_[hole] = slots_[j];
      slots_[j].occupied = false;
      hole = j;
    }
    return true;
  }

private:
  static constexpr std::size_t kInitialCapacity = 64;   // power of two
  static constexpr std::size_t kMaxLoadTenths = 7;      // grow past 70% full

  struct Slot {
    OrderId  key      = 0;
    uint32_t value    = 0;
    bool     occupied = false;
  };

  // splitmix64's finalizer. Order ids are typically sequential, and an identity
  // hash would map them to consecutive slots -- fine until ids arrive sparsely
  // or in a pattern that shares low bits, at which point linear probing degrades
  // badly. Five cheap ALU ops buy immunity to that.
  static uint64_t mix(OrderId key) {
    uint64_t x = key;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
  }

  std::size_t home(OrderId key) const {
    return static_cast<std::size_t>(mix(key)) & mask_;
  }

  void rehash(std::size_t capacity) {
    std::vector<Slot> old;
    old.swap(slots_);

    slots_.assign(capacity, Slot{});
    mask_ = capacity - 1;
    size_ = 0;

    // Safe from recursing: capacity is double the old one, and the old table
    // held at most 70% of its own capacity.
    for (const Slot &s : old) {
      if (s.occupied) {
        insert(s.key, s.value);
      }
    }
  }

  std::vector<Slot> slots_;
  std::size_t size_ = 0;
  std::size_t mask_ = 0;
};
