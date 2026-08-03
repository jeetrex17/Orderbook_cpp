#include "OrderBook.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

// The scenarios main.cpp used to demonstrate by printing, now asserted.

TEST(Matching, OrderWithNothingToMatchAgainstJustRests) {
  OrderBook book;
  const auto trades = book.processOrder(Order(1, 100, Side::Buy, 10));

  EXPECT_TRUE(trades.empty());
  ASSERT_TRUE(book.bestBid().has_value());
  EXPECT_EQ(*book.bestBid(), 100u);
  EXPECT_FALSE(book.bestAsk().has_value());
  EXPECT_EQ(book.restingQty(1).value_or(0), 10u);
}

TEST(Matching, ExactCrossLeavesAnEmptyBook) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  const auto trades = book.processOrder(Order(2, 100, Side::Sell, 10));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0], (Trade{2, 1, 100, 10}));
  EXPECT_FALSE(book.bestBid().has_value());
  EXPECT_FALSE(book.bestAsk().has_value());
  EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST(Matching, TakerRemainderRestsOnTheBook) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  const auto trades = book.processOrder(Order(2, 100, Side::Sell, 25));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].qty, 10u);
  // The 15 unfilled shares rest as an ask.
  ASSERT_TRUE(book.bestAsk().has_value());
  EXPECT_EQ(*book.bestAsk(), 100u);
  EXPECT_EQ(book.restingQty(2).value_or(0), 15u);
  EXPECT_FALSE(book.restingQty(1).has_value()); // fully filled, no longer resting
}

TEST(Matching, MakerRemainderStaysOnTheBook) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 40));
  const auto trades = book.processOrder(Order(2, 100, Side::Sell, 15));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].qty, 15u);
  EXPECT_EQ(book.restingQty(1).value_or(0), 25u);
  EXPECT_FALSE(book.restingQty(2).has_value());
}

TEST(Matching, TradesExecuteAtTheMakerPriceNotTheTakerPrice) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Sell, 10)); // maker offers at 100
  // The buyer is willing to pay 105, but the maker set the terms.
  const auto trades = book.processOrder(Order(2, 105, Side::Buy, 10));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].price, 100u);
}

TEST(Matching, NonOverlappingPricesDoNotCross) {
  OrderBook book;
  book.processOrder(Order(1, 99, Side::Buy, 10));
  const auto trades = book.processOrder(Order(2, 101, Side::Sell, 10));

  EXPECT_TRUE(trades.empty());
  EXPECT_EQ(*book.bestBid(), 99u);
  EXPECT_EQ(*book.bestAsk(), 101u);
}

TEST(Matching, PricePriorityBestLevelFillsFirst) {
  OrderBook book;
  book.processOrder(Order(1, 98, Side::Buy, 10));
  book.processOrder(Order(2, 100, Side::Buy, 10)); // best bid
  book.processOrder(Order(3, 99, Side::Buy, 10));

  const auto trades = book.processOrder(Order(4, 97, Side::Sell, 30));

  ASSERT_EQ(trades.size(), 3u);
  // Best price first, then progressively worse for the taker.
  EXPECT_EQ(trades[0].price, 100u);
  EXPECT_EQ(trades[1].price, 99u);
  EXPECT_EQ(trades[2].price, 98u);
  EXPECT_EQ(trades[0].maker_id, 2u);
  EXPECT_EQ(trades[1].maker_id, 3u);
  EXPECT_EQ(trades[2].maker_id, 1u);
}

TEST(Matching, TimePriorityFifoWithinAPriceLevel) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  book.processOrder(Order(2, 100, Side::Buy, 10));
  book.processOrder(Order(3, 100, Side::Buy, 10));

  EXPECT_EQ(book.ordersAtPrice(Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));

  const auto trades = book.processOrder(Order(4, 100, Side::Sell, 25));

  ASSERT_EQ(trades.size(), 3u);
  EXPECT_EQ(trades[0].maker_id, 1u);
  EXPECT_EQ(trades[1].maker_id, 2u);
  EXPECT_EQ(trades[2].maker_id, 3u);
  EXPECT_EQ(trades[2].qty, 5u); // order 3 is only partially filled
  EXPECT_EQ(book.restingQty(3).value_or(0), 5u);
}

TEST(Matching, SweepStopsAtTheTakerLimit) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Sell, 10));
  book.processOrder(Order(2, 101, Side::Sell, 10));
  book.processOrder(Order(3, 102, Side::Sell, 10));

  // The buyer will pay up to 101, so the 102 level must not trade.
  const auto trades = book.processOrder(Order(4, 101, Side::Buy, 100));

  ASSERT_EQ(trades.size(), 2u);
  EXPECT_EQ(trades[0].price, 100u);
  EXPECT_EQ(trades[1].price, 101u);
  // 80 unfilled shares rest at 101, above which the 102 ask still sits.
  EXPECT_EQ(book.restingQty(4).value_or(0), 80u);
  EXPECT_EQ(*book.bestAsk(), 102u);
  EXPECT_EQ(*book.bestBid(), 101u);
}

TEST(Matching, EmptiedPriceLevelIsRemoved) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  ASSERT_EQ(book.bidLevels().size(), 1u);

  book.processOrder(Order(2, 100, Side::Sell, 10));
  EXPECT_TRUE(book.bidLevels().empty());
  EXPECT_TRUE(book.askLevels().empty());
}

TEST(Matching, LevelViewReportsSummedQuantityNotOrderCount) {
  OrderBook book;
  book.processOrder(Order(1, 100, Side::Buy, 10));
  book.processOrder(Order(2, 100, Side::Buy, 32));

  const auto levels = book.bidLevels();
  ASSERT_EQ(levels.size(), 1u);
  EXPECT_EQ(levels[0].price, 100u);
  EXPECT_EQ(levels[0].qty, 42u); // 10 + 32, not the order count of 2
  EXPECT_EQ(levels[0].order_count, 2u);
}

TEST(Matching, LevelsAreReportedBestFirst) {
  OrderBook book;
  book.processOrder(Order(1, 98, Side::Buy, 1));
  book.processOrder(Order(2, 100, Side::Buy, 1));
  book.processOrder(Order(3, 99, Side::Buy, 1));
  book.processOrder(Order(4, 105, Side::Sell, 1));
  book.processOrder(Order(5, 103, Side::Sell, 1));
  book.processOrder(Order(6, 104, Side::Sell, 1));

  const auto bids = book.bidLevels();
  ASSERT_EQ(bids.size(), 3u);
  EXPECT_EQ(bids[0].price, 100u); // highest bid is best
  EXPECT_EQ(bids[1].price, 99u);
  EXPECT_EQ(bids[2].price, 98u);

  const auto asks = book.askLevels();
  ASSERT_EQ(asks.size(), 3u);
  EXPECT_EQ(asks[0].price, 103u); // lowest ask is best
  EXPECT_EQ(asks[1].price, 104u);
  EXPECT_EQ(asks[2].price, 105u);
}

TEST(Matching, OutParameterAppendsRatherThanClearing) {
  OrderBook book;
  std::vector<Trade> trades;

  book.processOrder(Order(1, 100, Side::Buy, 10), trades);
  book.processOrder(Order(2, 100, Side::Sell, 10), trades);
  book.processOrder(Order(3, 100, Side::Buy, 10), trades);
  book.processOrder(Order(4, 100, Side::Sell, 10), trades);

  // Two crossings, and the vector accumulated both.
  ASSERT_EQ(trades.size(), 2u);
  EXPECT_EQ(trades[0].taker_id, 2u);
  EXPECT_EQ(trades[1].taker_id, 4u);
}

} // namespace
