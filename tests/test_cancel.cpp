#include "OrderBook.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

TEST(Cancel, RemovesTheRestingOrder) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));

  EXPECT_TRUE(book.cancelOrder(1));
  EXPECT_FALSE(book.bestBid().has_value());
  EXPECT_FALSE(book.restingQty(1).has_value());
  EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST(Cancel, UnknownIdIsANoOp) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));

  EXPECT_FALSE(book.cancelOrder(999));
  EXPECT_EQ(book.restingQty(1).value_or(0), 10u); // book untouched
  EXPECT_EQ(book.restingOrderCount(), 1u);
}

TEST(Cancel, CancellingTwiceIsANoOpTheSecondTime) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));

  EXPECT_TRUE(book.cancelOrder(1));
  EXPECT_FALSE(book.cancelOrder(1));
}

TEST(Cancel, CancellingAFullyFilledOrderIsANoOp) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  book.processOrder(Order(2, 100, Side::Sell, 10)); // fills order 1 completely

  EXPECT_FALSE(book.cancelOrder(1));
  EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST(Cancel, CancelledOrderNeverFills) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  book.processOrder(Order(2, 100, Side::Buy, 10));
  ASSERT_TRUE(book.cancelOrder(1));

  const auto trades = book.processOrder(Order(3, 100, Side::Sell, 20));

  // Only order 2 is left to trade against.
  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].maker_id, 2u);
  EXPECT_EQ(trades[0].qty, 10u);
}

TEST(Cancel, PreservesFifoOrderOfTheRemainingOrders) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  book.processOrder(Order(2, 100, Side::Buy, 10));
  book.processOrder(Order(3, 100, Side::Buy, 10));

  ASSERT_TRUE(book.cancelOrder(2)); // cancel from the middle of the queue

  EXPECT_EQ(book.ordersAtPrice(Side::Buy, 100), (std::vector<OrderId>{1, 3}));

  const auto trades = book.processOrder(Order(4, 100, Side::Sell, 20));
  ASSERT_EQ(trades.size(), 2u);
  EXPECT_EQ(trades[0].maker_id, 1u);
  EXPECT_EQ(trades[1].maker_id, 3u);
}

TEST(Cancel, EmptiedPriceLevelIsRemoved) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  book.processOrder(Order(2, 99, Side::Buy, 10));
  ASSERT_EQ(book.bidLevels().size(), 2u);

  ASSERT_TRUE(book.cancelOrder(1));
  const auto levels = book.bidLevels();
  ASSERT_EQ(levels.size(), 1u);
  EXPECT_EQ(levels[0].price, 99u);
}

TEST(Cancel, WorksOnBothSides) {
  OrderBook book;
  book.processOrder(Order(1, 99, Side::Buy, 10));
  book.processOrder(Order(2, 101, Side::Sell, 10));

  EXPECT_TRUE(book.cancelOrder(2));
  EXPECT_FALSE(book.bestAsk().has_value());
  EXPECT_TRUE(book.bestBid().has_value());

  EXPECT_TRUE(book.cancelOrder(1));
  EXPECT_FALSE(book.bestBid().has_value());
}

// ---------------------------------------------------------------------------
// Regression: duplicate order ids
//
// Reusing a live order id used to overwrite that id's entry in orderPointers.
// The first order stayed on the book -- still matchable, but unreachable by id,
// so it could never be cancelled. Worse, when it later filled, its
// `orderPointers.erase(id)` removed the *second* order's tracking entry too,
// orphaning that one as well.
// ---------------------------------------------------------------------------

TEST(DuplicateId, SecondSubmissionIsRejected) {
  OrderBook book;
  std::vector<Trade> trades;

  EXPECT_EQ(book.processOrder(Order(1, 100, Side::Buy, 10), trades),
            SubmitResult::Accepted);
  EXPECT_EQ(book.processOrder(Order(1, 99, Side::Buy, 10), trades),
            SubmitResult::RejectedDuplicateId);

  // The rejected order left no trace: one resting order at the original price.
  EXPECT_EQ(book.restingOrderCount(), 1u);
  ASSERT_EQ(book.bidLevels().size(), 1u);
  EXPECT_EQ(book.bidLevels()[0].price, 100u);
}

TEST(DuplicateId, RejectionHappensBeforeAnyMatching) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Sell, 10));
  book.processOrder(Order(2, 100, Side::Sell, 10));

  std::vector<Trade> trades;
  // Order 2 already rests; resubmitting that id must not execute against
  // order 1 and then get rejected -- it must not execute at all.
  EXPECT_EQ(book.processOrder(Order(2, 105, Side::Buy, 10), trades),
            SubmitResult::RejectedDuplicateId);

  EXPECT_TRUE(trades.empty());
  EXPECT_EQ(book.restingQty(1).value_or(0), 10u);
  EXPECT_EQ(book.restingQty(2).value_or(0), 10u);
}

TEST(DuplicateId, EveryRestingOrderStaysReachableById) {
  // The invariant the old code broke: the set of orders sitting in the price
  // levels must exactly equal the set tracked in orderPointers.
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  book.processOrder(Order(1, 99, Side::Buy, 10));  // rejected
  book.processOrder(Order(2, 98, Side::Buy, 10));

  std::size_t orders_in_levels = 0;
  for (const auto& level : book.bidLevels()) {
    orders_in_levels += level.order_count;
  }
  for (const auto& level : book.askLevels()) {
    orders_in_levels += level.order_count;
  }
  EXPECT_EQ(orders_in_levels, book.restingOrderCount());

  // And every one of them can still be cancelled.
  EXPECT_TRUE(book.cancelOrder(1));
  EXPECT_TRUE(book.cancelOrder(2));
  EXPECT_EQ(book.restingOrderCount(), 0u);
  EXPECT_TRUE(book.bidLevels().empty());
}

TEST(DuplicateId, IdBecomesReusableOnceTheOrderIsNoLongerResting) {
  // Ids are only required to be unique among *live resting* orders. Tracking
  // every id ever seen would mean unbounded memory for no benefit.
  OrderBook book;
  std::vector<Trade> trades;

  book.processOrder(Order(1, 100, Side::Buy, 10), trades);
  book.processOrder(Order(2, 100, Side::Sell, 10), trades); // order 1 fully fills
  ASSERT_EQ(book.restingOrderCount(), 0u);

  EXPECT_EQ(book.processOrder(Order(1, 100, Side::Buy, 10), trades),
            SubmitResult::Accepted);

  // Same after a cancel.
  ASSERT_TRUE(book.cancelOrder(1));
  EXPECT_EQ(book.processOrder(Order(1, 100, Side::Buy, 10), trades),
            SubmitResult::Accepted);
}

TEST(DuplicateId, FullyFillingTakerWithALiveIdIsStillRejected) {
  // A taker that would fill completely never rests, so it would not have
  // corrupted orderPointers -- but accepting it would mean one id naming two
  // simultaneously live orders, which the rest of the API cannot express.
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10)); // order 1 rests as a bid
  book.processOrder(Order(2, 100, Side::Sell, 5)); // partially fills it

  std::vector<Trade> trades;
  EXPECT_EQ(book.processOrder(Order(1, 100, Side::Sell, 5), trades),
            SubmitResult::RejectedDuplicateId);
  EXPECT_TRUE(trades.empty());
  EXPECT_EQ(book.restingQty(1).value_or(0), 5u);
}

} // namespace
