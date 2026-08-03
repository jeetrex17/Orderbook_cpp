#pragma once

#include "Enums.h"
#include "IdIndex.h"
#include "Order.h"
#include "Trade.h"
#include "Types.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory_resource>
#include <optional>
#include <vector>

// Outcome of submitting an order. Rejection happens before any matching, so a
// rejected order never executes and never touches the book.
enum class SubmitResult {
  Accepted,
  RejectedDuplicateId,
};

// A read-only snapshot of one price level. The tests and Print() are written
// against this rather than against the underlying containers, so the storage
// layout can change without rewriting them.
struct LevelView {
  Price       price;
  Quantity    qty;         // summed remaining quantity at this level
  std::size_t order_count;
};

class OrderBook {
private:
  static constexpr uint32_t kNull = std::numeric_limits<uint32_t>::max();

  // One resting order.
  //
  // Orders live in a single slab, and price levels chain them by index rather
  // than by pointer. The previous design gave each price level its own
  // std::list, which meant one malloc per resting order and one free per fill:
  // cachegrind attributed 38.8% of all instructions to malloc.c. Indices into a
  // slab cost no allocation at all once the slab has grown, and neighbouring
  // orders tend to share cache lines instead of being scattered across the heap.
  struct Node {
    OrderId  id    = 0;
    Quantity qty   = 0;     // remaining, not initial
    Price    price = 0;     // the level this order sits at, so cancel can find it
    uint32_t next  = kNull; // doubles as the free-list link while this slot is free
    uint32_t prev  = kNull;
    Side     side  = Side::Buy;
  };

  // A FIFO queue of orders at one price, held as slab indices.
  struct Level {
    uint32_t head = kNull;
    uint32_t tail = kNull;
  };

  std::vector<Node> slab_;
  uint32_t free_head_ = kNull; // free-slot chain, threaded through Node::next

  // std::map allocates a node per price level, and levels are created and
  // destroyed constantly as a book trades -- in the profile that churn showed up
  // as ~10% of all instructions, essentially all of it inside malloc. A pool
  // resource recycles those fixed-size blocks instead of handing them back to
  // the system allocator, which removes the allocation traffic while keeping
  // the tree's O(log P) lookup.
  //
  // A flat sorted vector was tried here instead and rejected: it matched the
  // tree at ~200 price levels but was 1.7x slower at 2,000 and 6.3x slower at
  // 20,000, because every insert and erase became an O(P) memmove.
  std::pmr::unsynchronized_pool_resource pool_;
  std::pmr::map<Price, Level> asks_{&pool_};                      // lowest price is best
  std::pmr::map<Price, Level, std::greater<Price>> bids_{&pool_}; // highest price is best
  IdIndex index_;                                    // order id -> slab slot

  // -- slab management ------------------------------------------------------

  // NOTE: this can reallocate slab_, invalidating every outstanding Node
  // reference. Never hold one across a call to it.
  uint32_t allocNode() {
    if (free_head_ != kNull) {
      const uint32_t idx = free_head_;
      free_head_ = slab_[idx].next;
      return idx;
    }
    assert(slab_.size() < kNull && "slab index space exhausted");
    slab_.push_back(Node{});
    return static_cast<uint32_t>(slab_.size() - 1);
  }

  void freeNode(uint32_t idx) {
    slab_[idx].next = free_head_;
    free_head_ = idx;
  }

  // -- intrusive list operations --------------------------------------------

  void pushBack(Level &level, uint32_t idx) {
    slab_[idx].prev = level.tail;
    slab_[idx].next = kNull;
    if (level.tail != kNull) {
      slab_[level.tail].next = idx;
    } else {
      level.head = idx;
    }
    level.tail = idx;
  }

  void unlink(Level &level, uint32_t idx) {
    const uint32_t prev = slab_[idx].prev;
    const uint32_t next = slab_[idx].next;
    if (prev != kNull) {
      slab_[prev].next = next;
    } else {
      level.head = next;
    }
    if (next != kNull) {
      slab_[next].prev = prev;
    } else {
      level.tail = prev;
    }
  }

  template <typename LevelMap>
  std::vector<LevelView> snapshot(const LevelMap &levels) const {
    std::vector<LevelView> out;
    out.reserve(levels.size());
    for (const auto &[price, level] : levels) {
      Quantity qty = 0;
      std::size_t count = 0;
      for (uint32_t i = level.head; i != kNull; i = slab_[i].next) {
        qty += slab_[i].qty;
        ++count;
      }
      out.push_back(LevelView{price, qty, count});
    }
    return out;
  }

public:
  OrderBook() = default;

  // The price-level maps hold a pointer to this object's own pool, so a copied
  // or moved book would leave its maps allocating from the original's resource.
  OrderBook(const OrderBook &) = delete;
  OrderBook &operator=(const OrderBook &) = delete;
  OrderBook(OrderBook &&) = delete;
  OrderBook &operator=(OrderBook &&) = delete;

  // ------------------------------------------------------------------
  // Mutation
  // ------------------------------------------------------------------

  // Matches `incoming` against the book, appending each execution to `out`.
  // Whatever cannot be filled rests on the book. `out` is appended to, never
  // cleared, so callers can reuse one vector across calls and keep its capacity.
  //
  // An order whose id already belongs to a live resting order is rejected
  // outright. Without this check the new order would silently overwrite the old
  // one's entry in the id index, leaving the old order resting and matchable
  // but impossible to cancel -- and, when it later filled, erasing the *new*
  // order's tracking entry too.
  SubmitResult processOrder(const Order &incoming, std::vector<Trade> &out) {
    if (index_.find(incoming.orderid) != IdIndex::kMissing) {
      return SubmitResult::RejectedDuplicateId;
    }

    Quantity remaining = incoming.remaining_qty;

    if (incoming.side == Side::Buy) {
      while (remaining > 0 && !asks_.empty()) {
        auto levelIt = asks_.begin();
        const Price price = levelIt->first;

        if (price > incoming.price) { // nothing left that crosses
          break;
        }

        Level &level = levelIt->second;
        const uint32_t makerIdx = level.head;
        assert(makerIdx != kNull && "a live price level cannot be empty");

        const Quantity tradeQty = std::min(slab_[makerIdx].qty, remaining);
        remaining -= tradeQty;
        slab_[makerIdx].qty -= tradeQty;

        // Trades execute at the maker's price: the resting order set the terms.
        out.push_back(
            Trade{incoming.orderid, slab_[makerIdx].id, price, tradeQty});

        if (slab_[makerIdx].qty == 0) {
          index_.erase(slab_[makerIdx].id); // untrack before the slot is reused
          unlink(level, makerIdx);
          freeNode(makerIdx);
        }

        if (level.head == kNull) {
          asks_.erase(levelIt);
        }
      }
    } else {
      while (remaining > 0 && !bids_.empty()) {
        auto levelIt = bids_.begin();
        const Price price = levelIt->first;

        if (price < incoming.price) {
          break;
        }

        Level &level = levelIt->second;
        const uint32_t makerIdx = level.head;
        assert(makerIdx != kNull && "a live price level cannot be empty");

        const Quantity tradeQty = std::min(slab_[makerIdx].qty, remaining);
        remaining -= tradeQty;
        slab_[makerIdx].qty -= tradeQty;

        out.push_back(
            Trade{incoming.orderid, slab_[makerIdx].id, price, tradeQty});

        if (slab_[makerIdx].qty == 0) {
          index_.erase(slab_[makerIdx].id);
          unlink(level, makerIdx);
          freeNode(makerIdx);
        }

        if (level.head == kNull) {
          bids_.erase(levelIt);
        }
      }
    }

    // Whatever is left over rests on the book.
    if (remaining > 0) {
      const uint32_t idx = allocNode(); // may reallocate slab_
      slab_[idx].id = incoming.orderid;
      slab_[idx].qty = remaining;
      slab_[idx].price = incoming.price;
      slab_[idx].side = incoming.side;

      Level &level = (incoming.side == Side::Buy) ? bids_[incoming.price]
                                                  : asks_[incoming.price];
      pushBack(level, idx);
      index_.insert(incoming.orderid, idx);
    }

    return SubmitResult::Accepted;
  }

  // Convenience form for callers that don't care about reusing storage.
  std::vector<Trade> processOrder(const Order &incoming) {
    std::vector<Trade> trades;
    processOrder(incoming, trades);
    return trades;
  }

  // Removes a resting order. Cancelling an unknown, already-filled, or
  // already-cancelled id is a no-op. Returns whether anything was removed.
  bool cancelOrder(OrderId id) {
    const uint32_t idx = index_.find(id);
    if (idx == IdIndex::kMissing) {
      return false;
    }

    const Price price = slab_[idx].price;
    const Side side = slab_[idx].side;

    if (side == Side::Buy) {
      const auto levelIt = bids_.find(price);
      assert(levelIt != bids_.end() && "a tracked order must have a live level");
      unlink(levelIt->second, idx);
      if (levelIt->second.head == kNull) {
        bids_.erase(levelIt);
      }
    } else {
      const auto levelIt = asks_.find(price);
      assert(levelIt != asks_.end() && "a tracked order must have a live level");
      unlink(levelIt->second, idx);
      if (levelIt->second.head == kNull) {
        asks_.erase(levelIt);
      }
    }

    freeNode(idx);
    index_.erase(id);
    return true;
  }

  // ------------------------------------------------------------------
  // Inspection
  // ------------------------------------------------------------------

  std::optional<Price> bestBid() const {
    if (bids_.empty()) {
      return std::nullopt;
    }
    return bids_.begin()->first;
  }

  std::optional<Price> bestAsk() const {
    if (asks_.empty()) {
      return std::nullopt;
    }
    return asks_.begin()->first;
  }

  // Best-priced level first.
  std::vector<LevelView> bidLevels() const { return snapshot(bids_); }
  std::vector<LevelView> askLevels() const { return snapshot(asks_); }

  // Remaining quantity of a live resting order, or nullopt if it isn't resting.
  std::optional<Quantity> restingQty(OrderId id) const {
    const uint32_t idx = index_.find(id);
    if (idx == IdIndex::kMissing) {
      return std::nullopt;
    }
    return slab_[idx].qty;
  }

  std::size_t restingOrderCount() const { return index_.size(); }

  // Order ids at a price level, in queue (time-priority) order.
  std::vector<OrderId> ordersAtPrice(Side side, Price price) const {
    std::vector<OrderId> ids;

    uint32_t head = kNull;
    if (side == Side::Buy) {
      const auto it = bids_.find(price);
      if (it != bids_.end()) {
        head = it->second.head;
      }
    } else {
      const auto it = asks_.find(price);
      if (it != asks_.end()) {
        head = it->second.head;
      }
    }

    for (uint32_t i = head; i != kNull; i = slab_[i].next) {
      ids.push_back(slab_[i].id);
    }
    return ids;
  }

  void Print() const {
    std::cout << "--------------------- ASKS ---------------------\n";
    const auto ask_levels = askLevels();
    if (ask_levels.empty()) {
      std::cout << " (no asks)\n";
    } else {
      for (const LevelView &l : ask_levels) {
        std::cout << "Price: " << l.price << " | Qty: " << l.qty
                  << " | Orders in queue: " << l.order_count << "\n";
      }
    }

    std::cout << "\n--------------------- BIDS ---------------------\n";
    const auto bid_levels = bidLevels();
    if (bid_levels.empty()) {
      std::cout << " (no bids)\n";
    } else {
      for (const LevelView &l : bid_levels) {
        std::cout << "Price: " << l.price << " | Qty: " << l.qty
                  << " | Orders in queue: " << l.order_count << "\n";
      }
    }
    std::cout << "-----------------------------------------------\n\n";
  }
};
