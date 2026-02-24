#pragma once

#include "../include/Order.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// A deliberately simple / naive baseline OrderBook:
// - Stores bids and asks in vectors
// - Matches by repeatedly finding the best opposing price with linear scans
// This is meant for benchmarking comparison, not production.
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
  void processOrder(Order order) {
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

        const Quantity tradeQty =
            std::min(order.remaining_qty, best_it->remaining_qty);
        order.remaining_qty -= tradeQty;
        best_it->remaining_qty -= tradeQty;
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

        const Quantity tradeQty =
            std::min(order.remaining_qty, best_it->remaining_qty);
        order.remaining_qty -= tradeQty;
        best_it->remaining_qty -= tradeQty;
        if (best_it->remaining_qty == 0) {
          bids_.erase(best_it);
        }
      }

      if (order.remaining_qty > 0) {
        asks_.push_back(order);
      }
    }
  }
};

