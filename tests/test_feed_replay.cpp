#include "OrderBook.h"

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// insertResting: a feed 'Add' rests an order the exchange has already decided
// about, so it must not be matched here.
// ---------------------------------------------------------------------------

TEST(InsertResting, CrossingAddRestsInsteadOfMatching) {
  // The whole point of the method. Through processOrder() a buy at 600 would
  // sweep the 500 ask; through the feed path it must simply rest, because the
  // exchange already matched this order as far as it intended to and published
  // what was left.
  OrderBook book;
  book.processOrder(Order(1, 500, Side::Sell, 10));

  ASSERT_EQ(book.insertResting(Order(2, 600, Side::Buy, 10)),
            SubmitResult::Accepted);

  EXPECT_EQ(book.restingQty(1).value_or(0), 10u);
  EXPECT_EQ(book.restingQty(2).value_or(0), 10u);
  EXPECT_EQ(book.restingOrderCount(), 2u);

  // The book is now crossed, and that is correct for a replayed feed.
  ASSERT_TRUE(book.bestBid().has_value());
  ASSERT_TRUE(book.bestAsk().has_value());
  EXPECT_GT(*book.bestBid(), *book.bestAsk());
}

TEST(InsertResting, DuplicateIdIsRejectedAndTheOriginalSurvives) {
  OrderBook book;
  ASSERT_EQ(book.insertResting(Order(1, 100, Side::Buy, 10)),
            SubmitResult::Accepted);

  EXPECT_EQ(book.insertResting(Order(1, 99, Side::Buy, 55)),
            SubmitResult::RejectedDuplicateId);

  // Untouched: original price, original quantity, nothing extra resting.
  EXPECT_EQ(book.restingQty(1).value_or(0), 10u);
  EXPECT_EQ(book.restingOrderCount(), 1u);
  ASSERT_EQ(book.bidLevels().size(), 1u);
  EXPECT_EQ(book.bidLevels()[0].price, 100u);
}

TEST(InsertResting, RejectsADuplicateOfAnOrderRestedByProcessOrder) {
  // The two paths share one id index, so they must share one id space.
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));

  EXPECT_EQ(book.insertResting(Order(1, 100, Side::Buy, 10)),
            SubmitResult::RejectedDuplicateId);
  EXPECT_EQ(book.restingOrderCount(), 1u);
}

TEST(InsertResting, PreservesFifoWithinAPriceLevel) {
  OrderBook book;
  book.insertResting(Order(1, 100, Side::Buy, 10));
  book.insertResting(Order(2, 100, Side::Buy, 10));
  book.insertResting(Order(3, 100, Side::Buy, 10));

  EXPECT_EQ(book.ordersAtPrice(Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));
  ASSERT_EQ(book.bidLevels().size(), 1u);
  EXPECT_EQ(book.bidLevels()[0].qty, 30u);
  EXPECT_EQ(book.bidLevels()[0].order_count, 3u);
}

TEST(InsertResting, RestedOrderIsIndistinguishableFromOneRestedByProcessOrder) {
  // Once an order is on the book, how it got there must not change how it
  // matches -- including its place in the time-priority queue.
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10)); // via matching path
  book.insertResting(Order(2, 100, Side::Buy, 10)); // via feed path
  book.processOrder(Order(3, 100, Side::Buy, 10)); // via matching path

  EXPECT_EQ(book.ordersAtPrice(Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));

  const auto trades = book.processOrder(Order(4, 100, Side::Sell, 25));

  ASSERT_EQ(trades.size(), 3u);
  EXPECT_EQ(trades[0].maker_id, 1u);
  EXPECT_EQ(trades[1].maker_id, 2u); // the feed-rested order matches normally
  EXPECT_EQ(trades[2].maker_id, 3u);
  EXPECT_EQ(trades[2].qty, 5u);
}

// ---------------------------------------------------------------------------
// reduceQty: a feed 'Execute' reports a fill the exchange already performed.
// ---------------------------------------------------------------------------

TEST(ReduceQty, PartialFillDecrementsInPlaceAndKeepsQueuePosition) {
  OrderBook book;
  book.insertResting(Order(1, 100, Side::Buy, 100));
  book.insertResting(Order(2, 100, Side::Buy, 50));
  book.insertResting(Order(3, 100, Side::Buy, 50));

  EXPECT_TRUE(book.reduceQty(1, 40));

  EXPECT_EQ(book.restingQty(1).value_or(0), 60u);
  // Still first in the queue: a partial fill must not cost time priority.
  EXPECT_EQ(book.ordersAtPrice(Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));
  ASSERT_EQ(book.bidLevels().size(), 1u);
  EXPECT_EQ(book.bidLevels()[0].qty, 160u);

  // And it is still the first to fill.
  const auto trades = book.processOrder(Order(4, 100, Side::Sell, 10));
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].maker_id, 1u);
}

TEST(ReduceQty, FillExactlyEqualToRemainingRemovesTheOrder) {
  OrderBook book;
  book.insertResting(Order(1, 100, Side::Buy, 30));

  EXPECT_TRUE(book.reduceQty(1, 30));

  EXPECT_FALSE(book.restingQty(1).has_value());
  EXPECT_EQ(book.restingOrderCount(), 0u);
  EXPECT_TRUE(book.bidLevels().empty());
}

TEST(ReduceQty, FillGreaterThanRemainingRemovesTheOrderWithoutUnderflowing) {
  // Quantity is unsigned: a naive "qty -= fill" would wrap to ~1.8e19 here and
  // leave a phantom order with an astronomical size sitting on the book.
  OrderBook book;
  book.insertResting(Order(1, 100, Side::Buy, 10));

  EXPECT_TRUE(book.reduceQty(1, 999'999));

  EXPECT_FALSE(book.restingQty(1).has_value());
  EXPECT_EQ(book.restingOrderCount(), 0u);
  EXPECT_TRUE(book.bidLevels().empty());
}

TEST(ReduceQty, SuccessivePartialsAccumulate) {
  OrderBook book;
  book.insertResting(Order(1, 100, Side::Buy, 100));

  for (int i = 0; i < 9; ++i) {
    EXPECT_TRUE(book.reduceQty(1, 10)) << "fill " << i;
  }
  EXPECT_EQ(book.restingQty(1).value_or(0), 10u); // after the ninth

  EXPECT_TRUE(book.reduceQty(1, 10)); // the tenth empties it
  EXPECT_FALSE(book.restingQty(1).has_value());
  EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST(ReduceQty, UnknownOrDepartedIdReturnsFalseAndMutatesNothing) {
  // All three are normal on a feed joined mid-stream, not errors.
  OrderBook book;
  book.insertResting(Order(1, 100, Side::Buy, 10));
  book.insertResting(Order(2, 100, Side::Buy, 10));
  book.reduceQty(2, 10);                           // order 2 fully filled
  book.insertResting(Order(3, 100, Side::Buy, 10));
  book.cancelOrder(3);                             // order 3 cancelled

  EXPECT_FALSE(book.reduceQty(999, 5)); // never seen
  EXPECT_FALSE(book.reduceQty(2, 5));   // already filled
  EXPECT_FALSE(book.reduceQty(3, 5));   // already cancelled

  EXPECT_EQ(book.restingOrderCount(), 1u);
  EXPECT_EQ(book.restingQty(1).value_or(0), 10u);
  ASSERT_EQ(book.bidLevels().size(), 1u);
  EXPECT_EQ(book.bidLevels()[0].qty, 10u);
}

TEST(ReduceQty, EmptyingALevelErasesItAndMovesTheBestPrice) {
  OrderBook book;
  book.insertResting(Order(1, 100, Side::Buy, 10));
  book.insertResting(Order(2, 99, Side::Buy, 10));
  book.insertResting(Order(3, 200, Side::Sell, 10));
  book.insertResting(Order(4, 201, Side::Sell, 10));
  ASSERT_EQ(*book.bestBid(), 100u);
  ASSERT_EQ(*book.bestAsk(), 200u);

  EXPECT_TRUE(book.reduceQty(1, 10));
  EXPECT_EQ(book.bidLevels().size(), 1u);
  EXPECT_EQ(*book.bestBid(), 99u);

  EXPECT_TRUE(book.reduceQty(3, 10));
  EXPECT_EQ(book.askLevels().size(), 1u);
  EXPECT_EQ(*book.bestAsk(), 201u);
}

TEST(ReduceQty, WorksOnOrdersRestedThroughTheMatchingPath) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 40));

  EXPECT_TRUE(book.reduceQty(1, 15));
  EXPECT_EQ(book.restingQty(1).value_or(0), 25u);
}

// ---------------------------------------------------------------------------
// Property tests for the replay path.
//
// These deliberately do NOT reuse tests/TestOps.h. That generator feeds the
// matching-path properties, two of which the feed path legitimately violates:
//
//   - BookIsNeverCrossed. A feed can publish an add that crosses the book, and
//     insertResting must rest it rather than match it. "Never crossed" is a
//     property of the matching path, not of an order book as such.
//   - Differential.MatchesTheNaiveBaseline. SimpleOrderBook models matching
//     only; it has no notion of a feed message to compare against.
//
// Everything else still has to hold, so it is checked here against a generator
// that emits feed messages instead of orders.
// ---------------------------------------------------------------------------

struct FeedOp {
  uint8_t  kind;    // 0 = add, 1 = execute, 2 = cancel
  uint32_t pick;    // which previously-added order to act on
  uint64_t price;
  uint64_t qty;
  bool     is_buy;
};

std::ostream& operator<<(std::ostream& os, const FeedOp& op) {
  switch (op.kind) {
    case 0:  return os << "add(" << (op.is_buy ? "buy" : "sell") << " " << op.qty
                       << " @ " << op.price << ")";
    case 1:  return os << "execute(pick=" << op.pick << ", qty=" << op.qty << ")";
    default: return os << "cancel(pick=" << op.pick << ")";
  }
}

} // namespace

namespace rc {

template <>
struct Arbitrary<FeedOp> {
  static Gen<FeedOp> arbitrary() {
    return gen::build<FeedOp>(
        // Adds dominate, or the book drains faster than it fills and the
        // interesting states never appear.
        gen::set(&FeedOp::kind,
                 gen::weightedElement<uint8_t>({{5, 0}, {3, 1}, {1, 2}})),
        gen::set(&FeedOp::pick, gen::arbitrary<uint32_t>()),
        // A narrow band so adds cross each other constantly -- exactly the case
        // insertResting has to tolerate.
        gen::set(&FeedOp::price, gen::inRange<uint64_t>(95, 106)),
        gen::set(&FeedOp::qty, gen::inRange<uint64_t>(1, 51)),
        gen::set(&FeedOp::is_buy, gen::arbitrary<bool>()));
  }
};

} // namespace rc

namespace {

// Drives a generated feed through a book, then hands it to `check`.
template <typename F>
void replay(const std::vector<FeedOp>& ops, F&& after_each) {
  OrderBook book;
  std::vector<OrderId> added;
  OrderId next_id = 1;

  for (const FeedOp& op : ops) {
    if (op.kind == 0) {
      const OrderId id = next_id++;
      book.insertResting(
          Order(id, op.price, op.is_buy ? Side::Buy : Side::Sell, op.qty));
      added.push_back(id);
    } else if (!added.empty()) {
      const OrderId id = added[op.pick % added.size()];
      if (op.kind == 1) {
        book.reduceQty(id, op.qty);
      } else {
        book.cancelOrder(id);
      }
    }
    after_each(book);
  }
}

RC_GTEST_PROP(FeedReplay, EveryRestingOrderIsReachableById,
              (const std::vector<FeedOp>& ops)) {
  replay(ops, [](const OrderBook& book) {
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

RC_GTEST_PROP(FeedReplay, LevelsStaySortedNonEmptyAndInSubmissionOrder,
              (const std::vector<FeedOp>& ops)) {
  replay(ops, [](const OrderBook& book) {
    const auto bids = book.bidLevels();
    for (std::size_t i = 0; i < bids.size(); ++i) {
      RC_ASSERT(bids[i].qty > 0);
      RC_ASSERT(bids[i].order_count > 0);
      if (i > 0) {
        RC_ASSERT(bids[i - 1].price > bids[i].price);
      }
      const auto queue = book.ordersAtPrice(Side::Buy, bids[i].price);
      RC_ASSERT(queue.size() == bids[i].order_count);
      for (std::size_t j = 1; j < queue.size(); ++j) {
        RC_ASSERT(queue[j - 1] < queue[j]);
      }
    }

    const auto asks = book.askLevels();
    for (std::size_t i = 0; i < asks.size(); ++i) {
      RC_ASSERT(asks[i].qty > 0);
      RC_ASSERT(asks[i].order_count > 0);
      if (i > 0) {
        RC_ASSERT(asks[i - 1].price < asks[i].price);
      }
      const auto queue = book.ordersAtPrice(Side::Sell, asks[i].price);
      RC_ASSERT(queue.size() == asks[i].order_count);
      for (std::size_t j = 1; j < queue.size(); ++j) {
        RC_ASSERT(queue[j - 1] < queue[j]);
      }
    }
  });
}

RC_GTEST_PROP(FeedReplay, QuantityIsConserved, (const std::vector<FeedOp>& ops)) {
  OrderBook book;
  std::vector<OrderId> added;
  OrderId next_id = 1;

  std::unordered_map<OrderId, Quantity> initial;
  std::unordered_map<OrderId, Quantity> executed;
  std::unordered_map<OrderId, Quantity> removed;

  for (const FeedOp& op : ops) {
    if (op.kind == 0) {
      const OrderId id = next_id++;
      if (book.insertResting(Order(
              id, op.price, op.is_buy ? Side::Buy : Side::Sell, op.qty)) ==
          SubmitResult::Accepted) {
        initial[id] = op.qty;
      }
      added.push_back(id);
      continue;
    }
    if (added.empty()) {
      continue;
    }

    const OrderId id = added[op.pick % added.size()];
    const Quantity before = book.restingQty(id).value_or(0);

    if (op.kind == 1) {
      if (book.reduceQty(id, op.qty)) {
        // A fill at or beyond the remainder takes only the remainder: the
        // excess is not quantity that existed to be executed.
        executed[id] += (op.qty < before) ? op.qty : before;
      }
    } else if (book.cancelOrder(id)) {
      removed[id] += before;
    }
  }

  for (const auto& [id, qty] : initial) {
    const Quantity accounted =
        executed[id] + removed[id] + book.restingQty(id).value_or(0);
    RC_ASSERT(accounted == qty);
  }
}

} // namespace
