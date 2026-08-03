#include "OrderBook.h"
#include "TestOps.h"

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <cstddef>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }

// Drives a generated operation stream against a book, assigning order ids
// sequentially so they are unique by construction. `after_each` runs once per
// operation with the book and the trades that operation produced.
template <typename F>
void run(const std::vector<RawOp>& ops, F&& after_each) {
  OrderBook book;
  std::vector<OrderId> submitted;
  std::vector<Trade> trades;
  OrderId next_id = 1;

  for (const RawOp& op : ops) {
    trades.clear();

    if (op.is_cancel && !submitted.empty()) {
      book.cancelOrder(submitted[op.cancel_pick % submitted.size()]);
    } else {
      const OrderId id = next_id++;
      book.processOrder(
          Order(id, op.price, op.is_buy ? Side::Buy : Side::Sell, op.qty),
          trades);
      submitted.push_back(id);
    }

    after_each(book, trades);
  }
}

// ---------------------------------------------------------------------------
// Structural invariants: things that must hold after every single operation.
// ---------------------------------------------------------------------------

RC_GTEST_PROP(Invariants, BookIsNeverCrossed, (const std::vector<RawOp>& ops)) {
  run(ops, [](const OrderBook& book, const std::vector<Trade>&) {
    const auto bid = book.bestBid();
    const auto ask = book.bestAsk();
    if (bid && ask) {
      // Strict: equal prices would mean a match the engine failed to make.
      RC_ASSERT(*bid < *ask);
    }
  });
}

RC_GTEST_PROP(Invariants, LevelsAreSortedBestFirstAndNeverEmpty,
              (const std::vector<RawOp>& ops)) {
  run(ops, [](const OrderBook& book, const std::vector<Trade>&) {
    const auto bids = book.bidLevels();
    for (std::size_t i = 0; i < bids.size(); ++i) {
      RC_ASSERT(bids[i].qty > 0);         // an empty level must be dropped
      RC_ASSERT(bids[i].order_count > 0);
      if (i > 0) {
        RC_ASSERT(bids[i - 1].price > bids[i].price); // descending, no dupes
      }
    }

    const auto asks = book.askLevels();
    for (std::size_t i = 0; i < asks.size(); ++i) {
      RC_ASSERT(asks[i].qty > 0);
      RC_ASSERT(asks[i].order_count > 0);
      if (i > 0) {
        RC_ASSERT(asks[i - 1].price < asks[i].price); // ascending, no dupes
      }
    }
  });
}

// Time priority is two separate claims, and checking only the second one lets a
// LIFO insertion bug through: matching does consume the queue front-first, but
// the queue itself is in the wrong order. This property covers the first claim
// -- that queues are *built* in submission order. Ids are handed out in
// increasing order, so within a level they must increase monotonically.
RC_GTEST_PROP(Invariants, QueuesAreInSubmissionOrder,
              (const std::vector<RawOp>& ops)) {
  run(ops, [](const OrderBook& book, const std::vector<Trade>&) {
    for (const auto& [side, levels] :
         {std::pair{Side::Buy, book.bidLevels()},
          std::pair{Side::Sell, book.askLevels()}}) {
      for (const auto& level : levels) {
        const auto queue = book.ordersAtPrice(side, level.price);
        RC_ASSERT(queue.size() == level.order_count);
        for (std::size_t i = 1; i < queue.size(); ++i) {
          RC_ASSERT(queue[i - 1] < queue[i]);
        }
      }
    }
  });
}

// The id index and the price levels are two views of the same set of orders.
// When they drift apart you get ghost orders: resting and matchable, but
// unreachable by id and so impossible to cancel.
RC_GTEST_PROP(Invariants, EveryRestingOrderIsReachableById,
              (const std::vector<RawOp>& ops)) {
  run(ops, [](const OrderBook& book, const std::vector<Trade>&) {
    std::size_t in_levels = 0;
    for (const auto& level : book.bidLevels()) {
      in_levels += level.order_count;
      for (const OrderId id : book.ordersAtPrice(Side::Buy, level.price)) {
        RC_ASSERT(book.restingQty(id).has_value());
      }
    }
    for (const auto& level : book.askLevels()) {
      in_levels += level.order_count;
      for (const OrderId id : book.ordersAtPrice(Side::Sell, level.price)) {
        RC_ASSERT(book.restingQty(id).has_value());
      }
    }
    RC_ASSERT(in_levels == book.restingOrderCount());
  });
}

// ---------------------------------------------------------------------------
// Matching invariants: properties of the trades a submission produces.
// ---------------------------------------------------------------------------

RC_GTEST_PROP(Invariants, TradesRespectPriceAndTimePriority,
              (const std::vector<RawOp>& ops)) {
  OrderBook book;
  std::vector<OrderId> submitted;
  std::vector<Trade> trades;
  OrderId next_id = 1;

  for (const RawOp& op : ops) {
    if (op.is_cancel && !submitted.empty()) {
      book.cancelOrder(submitted[op.cancel_pick % submitted.size()]);
      continue;
    }

    const Side side = op.is_buy ? Side::Buy : Side::Sell;
    const Price limit = op.price;

    // Snapshot the queues the taker is about to eat into, so the trades can be
    // checked against the order the makers were actually standing in.
    std::map<Price, std::vector<OrderId>> queues_before;
    const auto opposite_levels =
        (side == Side::Buy) ? book.askLevels() : book.bidLevels();
    for (const auto& level : opposite_levels) {
      queues_before[level.price] = book.ordersAtPrice(opposite(side), level.price);
    }

    trades.clear();
    const OrderId id = next_id++;
    book.processOrder(Order(id, limit, side, op.qty), trades);
    submitted.push_back(id);

    std::map<Price, std::size_t> consumed;
    for (std::size_t i = 0; i < trades.size(); ++i) {
      const Trade& t = trades[i];
      RC_ASSERT(t.taker_id == id);
      RC_ASSERT(t.qty > 0);

      // The taker never trades through its own limit.
      if (side == Side::Buy) {
        RC_ASSERT(t.price <= limit);
      } else {
        RC_ASSERT(t.price >= limit);
      }

      // Price priority: each successive fill is at a price no better than the
      // last, i.e. the best available level is always consumed first.
      if (i > 0) {
        if (side == Side::Buy) {
          RC_ASSERT(t.price >= trades[i - 1].price);
        } else {
          RC_ASSERT(t.price <= trades[i - 1].price);
        }
      }

      // Time priority: the makers hit at a given price are exactly the front
      // of that price's queue, in order. A maker cannot appear twice in one
      // submission -- it either fills completely and leaves, or the taker runs
      // out -- so the makers form a prefix of the pre-trade queue.
      const auto queue = queues_before.find(t.price);
      RC_ASSERT(queue != queues_before.end());
      const std::size_t position = consumed[t.price]++;
      RC_ASSERT(position < queue->second.size());
      RC_ASSERT(t.maker_id == queue->second[position]);
    }
  }
}

// ---------------------------------------------------------------------------
// Accounting invariants: shares are neither created nor destroyed.
// ---------------------------------------------------------------------------

RC_GTEST_PROP(Invariants, QuantityIsConserved, (const std::vector<RawOp>& ops)) {
  OrderBook book;
  std::vector<OrderId> submitted;
  std::vector<Trade> trades;
  OrderId next_id = 1;

  std::unordered_map<OrderId, Quantity> initial;
  std::unordered_map<OrderId, Quantity> filled;
  std::unordered_map<OrderId, Quantity> cancelled_remainder;

  for (const RawOp& op : ops) {
    if (op.is_cancel && !submitted.empty()) {
      const OrderId id = submitted[op.cancel_pick % submitted.size()];
      const Quantity remainder = book.restingQty(id).value_or(0);
      if (book.cancelOrder(id)) {
        // Only a cancel that actually removed something takes quantity out of
        // circulation; cancelling an already-gone id is a no-op.
        cancelled_remainder[id] += remainder;
      }
      continue;
    }

    trades.clear();
    const OrderId id = next_id++;
    initial[id] = op.qty;
    book.processOrder(
        Order(id, op.price, op.is_buy ? Side::Buy : Side::Sell, op.qty), trades);
    submitted.push_back(id);

    for (const Trade& t : trades) {
      // Every execution moves the same quantity on both sides of the trade.
      filled[t.taker_id] += t.qty;
      filled[t.maker_id] += t.qty;
    }
  }

  for (const auto& [id, qty] : initial) {
    const Quantity accounted = filled[id] + book.restingQty(id).value_or(0) +
                               cancelled_remainder[id];
    RC_ASSERT(accounted == qty);
  }
}

RC_GTEST_PROP(Invariants, CancelledOrdersNeverTradeAgain,
              (const std::vector<RawOp>& ops)) {
  OrderBook book;
  std::vector<OrderId> submitted;
  std::vector<Trade> trades;
  std::unordered_set<OrderId> cancelled;
  OrderId next_id = 1;

  for (const RawOp& op : ops) {
    if (op.is_cancel && !submitted.empty()) {
      const OrderId id = submitted[op.cancel_pick % submitted.size()];
      if (book.cancelOrder(id)) {
        cancelled.insert(id);
        RC_ASSERT(!book.restingQty(id).has_value());
      }
      continue;
    }

    trades.clear();
    const OrderId id = next_id++;
    book.processOrder(
        Order(id, op.price, op.is_buy ? Side::Buy : Side::Sell, op.qty), trades);
    submitted.push_back(id);

    for (const Trade& t : trades) {
      RC_ASSERT(cancelled.find(t.maker_id) == cancelled.end());
      RC_ASSERT(cancelled.find(t.taker_id) == cancelled.end());
    }
  }
}

} // namespace
