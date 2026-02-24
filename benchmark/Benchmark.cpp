#include "../include/OrderBook.h"
#include "SimpleOrderBook.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
#include <string_view>
#include <vector>

namespace {

// This benchmark calls OrderBook::processOrder() many times. Since your OrderBook
// prints trades to stdout, we temporarily redirect std::cout to a "null" buffer
// so I/O doesn't dominate the benchmark time.
struct NullBuffer final : std::streambuf {
  int overflow(int ch) override { return ch; }
};

struct BenchmarkConfig final {
  std::size_t num_orders = 100'000;
  int runs = 5;

  Price price_lo = 1400;
  Price price_hi = 1600;
  Quantity qty_lo = 1;
  Quantity qty_hi = 1'000;

  uint64_t seed = 12345;
};

struct Result final {
  long long best_ms = 0;
  long long avg_ms = 0;
  long long avg_ns_per_order = 0;
};

std::vector<Order> generate_orders(const BenchmarkConfig& cfg) {
  if (cfg.num_orders % 2 != 0) {
    throw std::runtime_error("num_orders must be even (50% buys / 50% sells)");
  }

  std::mt19937_64 rng(cfg.seed);
  std::uniform_int_distribution<Price> price_dist(cfg.price_lo, cfg.price_hi);
  std::uniform_int_distribution<Quantity> qty_dist(cfg.qty_lo, cfg.qty_hi);

  std::vector<Order> orders;
  orders.reserve(cfg.num_orders);

  const std::size_t half = cfg.num_orders / 2;
  for (std::size_t i = 0; i < half; ++i) {
    const OrderId id = static_cast<OrderId>(i + 1);
    orders.emplace_back(id, price_dist(rng), Side::Buy, qty_dist(rng));
  }
  for (std::size_t i = 0; i < half; ++i) {
    const OrderId id = static_cast<OrderId>(half + i + 1);
    orders.emplace_back(id, price_dist(rng), Side::Sell, qty_dist(rng));
  }

  std::shuffle(orders.begin(), orders.end(), rng);
  return orders;
}

long long time_orderbook_once_ms(const std::vector<Order>& orders) {
  OrderBook book;
  const auto t0 = std::chrono::steady_clock::now();
  for (const auto& order : orders) {
    book.processOrder(order);
  }
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

long long time_simplebook_once_ms(const std::vector<Order>& orders) {
  SimpleOrderBook book;
  const auto t0 = std::chrono::steady_clock::now();
  for (const auto& order : orders) {
    book.processOrder(order);
  }
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

Result benchmark_orderbook(const BenchmarkConfig& cfg,
                           const std::vector<Order>& orders) {
  long long best = LLONG_MAX;
  long long sum = 0;

  for (int i = 0; i < cfg.runs; ++i) {
    const long long ms = time_orderbook_once_ms(orders);
    best = std::min(best, ms);
    sum += ms;
  }

  Result r;
  r.best_ms = best;
  r.avg_ms = sum / cfg.runs;
  r.avg_ns_per_order =
      (r.avg_ms * 1'000'000LL) / static_cast<long long>(orders.size());
  return r;
}

Result benchmark_simplebook(const BenchmarkConfig& cfg,
                            const std::vector<Order>& orders) {
  long long best = LLONG_MAX;
  long long sum = 0;

  for (int i = 0; i < cfg.runs; ++i) {
    const long long ms = time_simplebook_once_ms(orders);
    best = std::min(best, ms);
    sum += ms;
  }

  Result r;
  r.best_ms = best;
  r.avg_ms = sum / cfg.runs;
  r.avg_ns_per_order =
      (r.avg_ms * 1'000'000LL) / static_cast<long long>(orders.size());
  return r;
}

void print_row(std::string_view name, const Result& r) {
  std::cout << std::left << std::setw(22) << name << " | " << std::right
            << std::setw(10) << r.best_ms << " | " << std::setw(10) << r.avg_ms
            << " | " << std::setw(14) << r.avg_ns_per_order << "\n";
}

} // namespace

int main() {
  const BenchmarkConfig cfg{};

  // Pre-generate orders so creation doesn't pollute OrderBook timing.
  const auto orders = generate_orders(cfg);

  // my better OrderBook prints trades so we silence stdout during the benchmark.
  NullBuffer null_buf;
  std::ostream null_out(&null_buf);
  std::streambuf* old_cout_buf = std::cout.rdbuf(null_out.rdbuf());

  const Result mine = benchmark_orderbook(cfg, orders);
  const Result simple = benchmark_simplebook(cfg, orders);

  std::cout.rdbuf(old_cout_buf);

  std::cout << "OrderBook benchmark\n";
  std::cout << "- Orders: " << cfg.num_orders << " (50% buys / 50% sells)\n";
  std::cout << "- Price: [" << cfg.price_lo << ", " << cfg.price_hi << "]\n";
  std::cout << "- Qty:   [" << cfg.qty_lo << ", " << cfg.qty_hi << "]\n";
  std::cout << "- Seed:  " << cfg.seed << "\n";
  std::cout << "- Runs:  " << cfg.runs << "\n\n";

  std::cout << std::left << std::setw(22) << "Implementation"
            << " | " << std::right << std::setw(10) << "best(ms)"
            << " | " << std::setw(10) << "avg(ms)"
            << " | " << std::setw(14) << "avg(ns/order)" << "\n";
  std::cout << std::string(22, '-') << "-+-" << std::string(10, '-') << "-+-"
            << std::string(10, '-') << "-+-" << std::string(14, '-') << "\n";

  print_row("improved OrderBook", mine);
  print_row("Simple baseline", simple);

  return 0;
}
