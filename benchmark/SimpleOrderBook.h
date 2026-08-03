#pragma once

#include "../include/Order.h"
#include "../include/Trade.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// A deliberately simple / naive baseline OrderBook:
// - Stores bids and asks in vectors
// - Matches by repeatedly finding the best opposing price with linear scans
// This is meant for benchmarking comparison, not production.
//
// It must stay behaviourally identical to OrderBook for the add/match path, so
// tests/prop_differential.cpp can use it as a reference implementation. In
// particular it preserves FIFO among equal prices: the price comparison below
// is strict, so the first-inserted order of a tied price stays the best, and
// the vectors are insertion-ordered.
class SimpleOrderBook {
private:
  std::vector<Order> bids_;
  std::vector<Order> asks_;

  static bool is_crossing(const Order& taker, const Order& maker) {
    if (taker.side == Side::Buy) {
      return maker.side == Side::Sell && maker.price <= taker.price;
    }
    return maker.side == Side::Buy && maker.price >= taker.price;
  }

public:
  // Same contract as OrderBook::processOrder: appends executions to `out`.
  void processOrder(const Order& incoming, std::vector<Trade>& out) {
    Order order = incoming;

    if (order.side == Side::Buy) {
      while (order.remaining_qty > 0) {
        // Find best ask (lowest price) that crosses.
        auto best_it = asks_.end();
        for (auto it = asks_.begin(); it != asks_.end(); ++it) {
          if (!is_crossing(order, *it) || it->remaining_qty == 0) {
            continue;
          }
          if (best_it == asks_.end() || it->price < best_it->price) {
            best_it = it;
          }
        }

        if (best_it == asks_.end()) {
          break;
        }

        const Quantity tradeQty = std::min(order.remaining_qty, best_it->remaining_qty);
        order.remaining_qty -= tradeQty;
        best_it->remaining_qty -= tradeQty;
        out.push_back(Trade{order.orderid, best_it->orderid, best_it->price, tradeQty});
        if (best_it->remaining_qty == 0) {
          asks_.erase(best_it);
        }
      }

      if (order.remaining_qty > 0) {
        bids_.push_back(order);
      }
    } else { // Sell
      while (order.remaining_qty > 0) {
        // Find best bid (highest price) that crosses.
        auto best_it = bids_.end();
        for (auto it = bids_.begin(); it != bids_.end(); ++it) {
          if (!is_crossing(order, *it) || it->remaining_qty == 0) {
            continue;
          }
          if (best_it == bids_.end() || it->price > best_it->price) {
            best_it = it;
          }
        }

        if (best_it == bids_.end()) {
          break;
        }

        const Quantity tradeQty = std::min(order.remaining_qty, best_it->remaining_qty);
        order.remaining_qty -= tradeQty;
        best_it->remaining_qty -= tradeQty;
        out.push_back(Trade{order.orderid, best_it->orderid, best_it->price, tradeQty});
        if (best_it->remaining_qty == 0) {
          bids_.erase(best_it);
        }
      }

      if (order.remaining_qty > 0) {
        asks_.push_back(order);
      }
    }
  }

  std::vector<Trade> processOrder(const Order& incoming) {
    std::vector<Trade> trades;
    processOrder(incoming, trades);
    return trades;
  }

  // Naive to match the rest of this class: a linear scan of both sides rather
  // than OrderBook's O(1) id lookup. Exists so the differential test can cover
  // the cancel path too, not just add/match. The benchmark never calls it.
  bool cancelOrder(OrderId id) {
    for (auto* book : {&bids_, &asks_}) {
      const auto it = std::find_if(book->begin(), book->end(),
                                   [id](const Order& o) { return o.orderid == id; });
      if (it != book->end()) {
        book->erase(it);
        return true;
      }
    }
    return false;
  }
};
