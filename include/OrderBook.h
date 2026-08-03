#pragma once

#include "Enums.h"
#include "Order.h"
#include "Trade.h"
#include "Types.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
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
  std::map<Price, std::list<Order>> asks; // sellers: lowest price is best
  std::map<Price, std::list<Order>, std::greater<Price>> bids; // buyers: highest price is best
  std::unordered_map<OrderId, std::list<Order>::iterator> orderPointers; // order id -> exact node, so cancel is O(1)

  template <typename LevelMap>
  static std::vector<LevelView> snapshot(const LevelMap &levels) {
    std::vector<LevelView> out;
    out.reserve(levels.size());
    for (const auto &[price, orders] : levels) {
      Quantity qty = 0;
      for (const Order &o : orders) {
        qty += o.remaining_qty;
      }
      out.push_back(LevelView{price, qty, orders.size()});
    }
    return out;
  }

public:
  // ------------------------------------------------------------------
  // Mutation
  // ------------------------------------------------------------------

  // Matches `incoming` against the book, appending each execution to `out`.
  // Whatever cannot be filled rests on the book. `out` is appended to, never
  // cleared, so callers can reuse one vector across calls and keep its capacity.
  //
  // An order whose id already belongs to a live resting order is rejected
  // outright. Without this check the new order would silently overwrite the old
  // one's entry in orderPointers, leaving the old order resting and matchable
  // but impossible to cancel -- and, when it later filled, erasing the *new*
  // order's tracking entry too.
  SubmitResult processOrder(const Order &incoming, std::vector<Trade> &out) {
    if (orderPointers.find(incoming.orderid) != orderPointers.end()) {
      return SubmitResult::RejectedDuplicateId;
    }

    Quantity remaining = incoming.remaining_qty;

    if (incoming.side == Side::Buy) {
      while (remaining > 0 && !asks.empty()) {
        auto bestAskIter = asks.begin();
        const Price bestAskPrice = bestAskIter->first;
        std::list<Order> &askQueue = bestAskIter->second;

        if (bestAskPrice > incoming.price) { // nothing left that crosses
          break;
        }

        Order &restingAsk = askQueue.front();

        const Quantity tradeQty = std::min(restingAsk.remaining_qty, remaining);
        remaining -= tradeQty;
        restingAsk.remaining_qty -= tradeQty;

        // Trades execute at the maker's price: the resting order set the terms.
        out.push_back(Trade{incoming.orderid, restingAsk.orderid, bestAskPrice,
                            tradeQty});

        // If the resting ask is fully filled, remove it from the queue.
        if (restingAsk.remaining_qty == 0) {
          orderPointers.erase(restingAsk.orderid); // stop tracking BEFORE the node dies
          askQueue.pop_front();
        }

        // If the queue for this price is now empty, drop the price level.
        if (askQueue.empty()) {
          asks.erase(bestAskIter);
        }
      }

      // Whatever is left over rests on the book.
      if (remaining > 0) {
        Order resting = incoming;
        resting.remaining_qty = remaining;
        auto &queue = bids[incoming.price];
        auto it = queue.insert(queue.end(), resting); // insert returns an iterator to the new node
        orderPointers[incoming.orderid] = it;
      }
    } else {
      while (remaining > 0 && !bids.empty()) {
        auto bestBidIter = bids.begin();
        const Price bestBidPrice = bestBidIter->first;
        std::list<Order> &bidQueue = bestBidIter->second;

        if (bestBidPrice < incoming.price) {
          break;
        }

        Order &restingBid = bidQueue.front();

        const Quantity tradeQty = std::min(restingBid.remaining_qty, remaining);
        remaining -= tradeQty;
        restingBid.remaining_qty -= tradeQty;

        out.push_back(Trade{incoming.orderid, restingBid.orderid, bestBidPrice,
                            tradeQty});

        if (restingBid.remaining_qty == 0) {
          orderPointers.erase(restingBid.orderid);
          bidQueue.pop_front();
        }

        if (bidQueue.empty()) {
          bids.erase(bestBidIter);
        }
      }

      if (remaining > 0) {
        Order resting = incoming;
        resting.remaining_qty = remaining;
        auto &queue = asks[incoming.price];
        auto it = queue.insert(queue.end(), resting);
        orderPointers[incoming.orderid] = it;
      }
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
    auto mapIt = orderPointers.find(id);
    if (mapIt == orderPointers.end()) {
      return false;
    }

    auto listIterator = mapIt->second;       // std::list<Order>::iterator
    const Price price = listIterator->price; // the price level it lives at
    const Side side = listIterator->side;

    if (side == Side::Buy) {
      auto priceIt = bids.find(price);
      if (priceIt != bids.end()) {
        auto &orderList = priceIt->second;
        orderList.erase(listIterator);

        if (orderList.empty()) {
          bids.erase(priceIt);
        }
      }
    } else { // Side::Sell
      auto priceIt = asks.find(price);
      if (priceIt != asks.end()) {
        auto &orderList = priceIt->second;
        orderList.erase(listIterator);

        if (orderList.empty()) {
          asks.erase(priceIt);
        }
      }
    }

    orderPointers.erase(mapIt);
    return true;
  }

  // ------------------------------------------------------------------
  // Inspection
  // ------------------------------------------------------------------

  std::optional<Price> bestBid() const {
    if (bids.empty()) {
      return std::nullopt;
    }
    return bids.begin()->first;
  }

  std::optional<Price> bestAsk() const {
    if (asks.empty()) {
      return std::nullopt;
    }
    return asks.begin()->first;
  }

  // Best-priced level first.
  std::vector<LevelView> bidLevels() const { return snapshot(bids); }
  std::vector<LevelView> askLevels() const { return snapshot(asks); }

  // Remaining quantity of a live resting order, or nullopt if it isn't resting.
  std::optional<Quantity> restingQty(OrderId id) const {
    auto it = orderPointers.find(id);
    if (it == orderPointers.end()) {
      return std::nullopt;
    }
    return it->second->remaining_qty;
  }

  std::size_t restingOrderCount() const { return orderPointers.size(); }

  // Order ids at a price level, in queue (time-priority) order.
  std::vector<OrderId> ordersAtPrice(Side side, Price price) const {
    std::vector<OrderId> ids;
    if (side == Side::Buy) {
      auto it = bids.find(price);
      if (it != bids.end()) {
        for (const Order &o : it->second) {
          ids.push_back(o.orderid);
        }
      }
    } else {
      auto it = asks.find(price);
      if (it != asks.end()) {
        for (const Order &o : it->second) {
          ids.push_back(o.orderid);
        }
      }
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
