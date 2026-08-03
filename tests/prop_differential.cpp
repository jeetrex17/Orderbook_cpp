#include "OrderBook.h"
#include "SimpleOrderBook.h"
#include "TestOps.h"

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <cstddef>
#include <vector>

namespace {

// The strongest test in the suite: run the same operation stream through the
// optimised book and the deliberately naive vector-scanning baseline, and
// require byte-for-byte identical executions.
//
// Identical trade sequences also pin the resting state: both books saw the same
// submissions and produced the same fills, so every order's remaining quantity
// must agree too. That is what makes this a full equivalence check rather than
// just a check on output.
//
// Ids are assigned sequentially and so are unique. Duplicate-id rejection is
// OrderBook-only behaviour that the baseline does not model, and it has its own
// tests in test_cancel.cpp.
RC_GTEST_PROP(Differential, MatchesTheNaiveBaseline,
              (const std::vector<RawOp>& ops)) {
  OrderBook fast;
  SimpleOrderBook slow;

  std::vector<OrderId> submitted;
  std::vector<Trade> fast_trades;
  std::vector<Trade> slow_trades;
  OrderId next_id = 1;

  for (const RawOp& op : ops) {
    if (op.is_cancel && !submitted.empty()) {
      const OrderId id = submitted[op.cancel_pick % submitted.size()];
      const bool fast_removed = fast.cancelOrder(id);
      const bool slow_removed = slow.cancelOrder(id);
      RC_ASSERT(fast_removed == slow_removed);
      continue;
    }

    const OrderId id = next_id++;
    const Order order(id, op.price, op.is_buy ? Side::Buy : Side::Sell, op.qty);

    fast_trades.clear();
    slow_trades.clear();
    fast.processOrder(order, fast_trades);
    slow.processOrder(order, slow_trades);
    submitted.push_back(id);

    RC_ASSERT(fast_trades.size() == slow_trades.size());
    for (std::size_t i = 0; i < fast_trades.size(); ++i) {
      // Compared field by field so a failure names the field that diverged.
      RC_ASSERT(fast_trades[i].taker_id == slow_trades[i].taker_id);
      RC_ASSERT(fast_trades[i].maker_id == slow_trades[i].maker_id);
      RC_ASSERT(fast_trades[i].price == slow_trades[i].price);
      RC_ASSERT(fast_trades[i].qty == slow_trades[i].qty);
    }
  }
}

} // namespace
