# Orderbook_cpp

A simple C++  orderbook (matching engine).

## How it works 

- The book stores **asks** as `std::map<Price, std::list<Order>>` (best ask = lowest price).
- The book stores **bids** as `std::map<Price, std::list<Order>, std::greater<Price>>` (best bid = highest price).
- Each price level keeps orders in a `std::list` to preserve FIFO at that price.
- Incoming orders match the best opposing price level while the prices cross:
  - Buy crosses if `bestAskPrice <= buyPrice`
  - Sell crosses if `bestBidPrice >= sellPrice`

## Why this is faster than the naive baseline (time complexity)

The baseline in `benchmark/SimpleOrderBook.h` stores bids/asks in `std::vector` and, for each fill step, it **linearly scans** the whole opposite side to find the best price that crosses.

In `include/OrderBook.h`, we keep price levels sorted:

- **Best price lookup**
  - This OrderBook: `asks.begin()` / `bids.begin()` → **O(1)** to get the best price level.
  - Baseline: scan all orders to find best crossing → **O(N)**.
- **Add order**
  - This OrderBook: insert/find price level in `std::map` → **O(log P)** where `P` = number of price levels.
  - Baseline: `push_back` → **O(1)** (but matching later is expensive).
- **Cancel order by id**
  - This OrderBook: `unordered_map` lookup + erase by `list` iterator → **O(1)** average (plus `O(log P)` to find the price level map entry).
  - Baseline: would require scanning to find the order → **O(N)**.

In short: My Implementation of OrderBook reduces repeated full-book scans during matching, which is why its much faster than the naive baseline.


## Benchmark result (1 run)

This is the output from a single benchmark execution (the benchmark itself runs 5 timed runs internally and reports best/avg).

```text
OrderBook benchmark
- Orders: 100000 (50% buys / 50% sells)
- Price: [1400, 1600]
- Qty:   [1, 1000]
- Seed:  12345
- Runs:  5

Implementation         |   best(ms) |    avg(ms) |  avg(ns/order)
-----------------------+------------+------------+---------------
improved OrderBook     |         32 |         32 |            320
Simple baseline        |       1188 |       1213 |          12130
```
