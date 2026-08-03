#include "../include/OrderBook.h"
#include "../include/Trade.h"
#include "SimpleOrderBook.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct BenchmarkConfig final {
  std::size_t num_orders = 100'000;
  int runs = 5;

  Price price_lo = 1400;
  Price price_hi = 1600;
  Quantity qty_lo = 1;
  Quantity qty_hi = 1'000;

  uint64_t seed = 12345;
};

// Timed in nanoseconds. At millisecond granularity a 100k-order run of the fast
// book lands on ~7ms, which is one significant figure -- not enough to tell a
// 20% improvement from noise.
struct Result final {
  long long best_ns = 0;
  long long avg_ns = 0;
  double ns_per_order = 0.0;
  std::size_t trades = 0;
};

// std::uniform_int_distribution and std::shuffle are only specified in terms of
// their distribution, not their exact output, so libc++ and libstdc++ produce
// different sequences from the same seed. That would make the wall-clock runs
// (macOS/libc++) and the cachegrind runs (Linux/libstdc++) measure two
// different workloads. Reducing the generator output by hand keeps the order
// stream byte-identical everywhere. The modulo bias is irrelevant here.
Price bounded(std::mt19937_64& rng, uint64_t lo, uint64_t hi) {
  return lo + rng() % (hi - lo + 1);
}

std::vector<Order> generate_orders(const BenchmarkConfig& cfg) {
  if (cfg.num_orders % 2 != 0) {
    throw std::runtime_error("num_orders must be even (50% buys / 50% sells)");
  }

  std::mt19937_64 rng(cfg.seed);

  std::vector<Order> orders;
  orders.reserve(cfg.num_orders);

  const std::size_t half = cfg.num_orders / 2;
  for (std::size_t i = 0; i < half; ++i) {
    const OrderId id = static_cast<OrderId>(i + 1);
    const Price price = bounded(rng, cfg.price_lo, cfg.price_hi);
    const Quantity qty = bounded(rng, cfg.qty_lo, cfg.qty_hi);
    orders.emplace_back(id, price, Side::Buy, qty);
  }
  for (std::size_t i = 0; i < half; ++i) {
    const OrderId id = static_cast<OrderId>(half + i + 1);
    const Price price = bounded(rng, cfg.price_lo, cfg.price_hi);
    const Quantity qty = bounded(rng, cfg.qty_lo, cfg.qty_hi);
    orders.emplace_back(id, price, Side::Sell, qty);
  }

  // Fisher-Yates by hand, for the same reason.
  for (std::size_t i = orders.size(); i > 1; --i) {
    const std::size_t j = static_cast<std::size_t>(rng() % i);
    std::swap(orders[i - 1], orders[j]);
  }

  return orders;
}

// Both books append executions into a caller-owned vector, so the timed loop
// does no I/O and (after the first few orders) no allocation either.
template <typename Book>
long long time_book_once_ns(const std::vector<Order>& orders,
                            std::size_t& trade_count_out) {
  Book book;
  std::vector<Trade> trades;
  trades.reserve(64);

  std::size_t total_trades = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (const auto& order : orders) {
    trades.clear();
    book.processOrder(order, trades);
    total_trades += trades.size();
  }
  const auto t1 = std::chrono::steady_clock::now();

  trade_count_out = total_trades;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

template <typename Book>
Result benchmark(const BenchmarkConfig& cfg, const std::vector<Order>& orders) {
  long long best = LLONG_MAX;
  long long sum = 0;
  std::size_t trades = 0;

  for (int i = 0; i < cfg.runs; ++i) {
    const long long ns = time_book_once_ns<Book>(orders, trades);
    best = std::min(best, ns);
    sum += ns;
  }

  Result r;
  r.best_ns = best;
  r.avg_ns = sum / cfg.runs;
  // Reported from the best run, not the average: the fastest observed time is
  // the one least polluted by scheduling and background load.
  r.ns_per_order =
      static_cast<double>(best) / static_cast<double>(orders.size());
  r.trades = trades;
  return r;
}

void print_row(std::string_view name, const Result& r) {
  std::cout << std::left << std::setw(22) << name << " | " << std::right
            << std::fixed << std::setprecision(2) << std::setw(10)
            << static_cast<double>(r.best_ns) / 1e6 << " | " << std::setw(10)
            << static_cast<double>(r.avg_ns) / 1e6 << " | " << std::setw(14)
            << r.ns_per_order << "\n";
}

[[noreturn]] void usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " [--orders N] [--runs N] [--seed N] [--baseline yes|no]"
               " [--price-lo N] [--price-hi N]\n";
  std::exit(2);
}

unsigned long long parse_ull(std::string_view s, const char* argv0) {
  unsigned long long value = 0;
  const auto* first = s.data();
  const auto* last = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(first, last, value);
  if (ec != std::errc{} || ptr != last) {
    usage(argv0);
  }
  return value;
}

} // namespace

int main(int argc, char** argv) {
  BenchmarkConfig cfg{};
  // The naive baseline is O(N) per order, so it dominates wall-clock at large
  // order counts. Profiling runs want it off; the README comparison wants it on.
  bool run_baseline = true;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const auto next = [&]() -> std::string_view {
      if (i + 1 >= argc) {
        usage(argv[0]);
      }
      return argv[++i];
    };

    if (arg == "--orders") {
      cfg.num_orders = static_cast<std::size_t>(parse_ull(next(), argv[0]));
    } else if (arg == "--runs") {
      cfg.runs = static_cast<int>(parse_ull(next(), argv[0]));
    } else if (arg == "--price-lo") {
      cfg.price_lo = parse_ull(next(), argv[0]);
    } else if (arg == "--price-hi") {
      cfg.price_hi = parse_ull(next(), argv[0]);
    } else if (arg == "--seed") {
      cfg.seed = parse_ull(next(), argv[0]);
    } else if (arg == "--baseline") {
      const std::string_view v = next();
      run_baseline = (v == "yes" || v == "1" || v == "true");
    } else {
      usage(argv[0]);
    }
  }

  if (cfg.runs < 1 || cfg.price_hi < cfg.price_lo) {
    usage(argv[0]);
  }

  // Pre-generate orders so creation doesn't pollute the timed section.
  const auto orders = generate_orders(cfg);

  const Result mine = benchmark<OrderBook>(cfg, orders);

  std::cout << "OrderBook benchmark\n";
  std::cout << "- Orders: " << cfg.num_orders << " (50% buys / 50% sells)\n";
  // The width of the price band sets how many live price levels the book
  // carries, which is the variable the level directory is sensitive to.
  std::cout << "- Price: [" << cfg.price_lo << ", " << cfg.price_hi << "] ("
            << (cfg.price_hi - cfg.price_lo + 1) << " ticks)\n";
  std::cout << "- Qty:   [" << cfg.qty_lo << ", " << cfg.qty_hi << "]\n";
  std::cout << "- Seed:  " << cfg.seed << "\n";
  std::cout << "- Runs:  " << cfg.runs << "\n";
  std::cout << "- Trades executed per run: " << mine.trades << "\n\n";

  std::cout << std::left << std::setw(22) << "Implementation"
            << " | " << std::right << std::setw(10) << "best(ms)"
            << " | " << std::setw(10) << "avg(ms)"
            << " | " << std::setw(14) << "avg(ns/order)" << "\n";
  std::cout << std::string(22, '-') << "-+-" << std::string(10, '-') << "-+-"
            << std::string(10, '-') << "-+-" << std::string(14, '-') << "\n";

  print_row("improved OrderBook", mine);

  if (run_baseline) {
    const Result simple = benchmark<SimpleOrderBook>(cfg, orders);
    print_row("Simple baseline", simple);

    if (simple.trades != mine.trades) {
      std::cerr << "\nWARNING: trade counts differ (OrderBook " << mine.trades
                << " vs baseline " << simple.trades
                << ") -- the two books disagree.\n";
      return 1;
    }
  }

  return 0;
}
