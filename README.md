# Orderbook_cpp

A C++20 limit-order matching engine, with a test suite, sanitizers, CI, and a
measured optimisation pass.

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
./build/dev/orderbook_demo
```

## How it works

Orders match on **price-time priority**: the best price fills first, and among
orders at the same price the one that arrived first fills first.

- Bids are kept in a `std::map<Price, Level, std::greater<Price>>` — highest
  price is best, so `begin()` is the best bid.
- Asks are kept in a `std::map<Price, Level>` — lowest price is best.
- Each `Level` is a FIFO queue of order slots, held as head/tail indices into a
  single slab (`std::vector<Node>`) shared by the whole book.
- `IdIndex`, an open-addressing hash map, maps each live order id to its slab
  slot, so cancelling is O(1) without searching the book.

Matching produces `Trade` values, returned to the caller:

```cpp
OrderBook book;
std::vector<Trade> trades;
book.processOrder(Order(1, 100, Side::Buy, 10), trades);
book.cancelOrder(1);
```

Trades execute at the **maker's** price — the resting order set the terms — so a
buyer willing to pay 105 who hits a resting ask at 100 trades at 100.

Order ids must be unique among *live resting* orders; a duplicate is rejected
before any matching happens, so a rejected order never executes. Once an order
has fully filled or been cancelled, its id is free to reuse.

## Correctness

43 tests: unit tests for matching and cancellation, property tests, and a
differential test.

**Property tests** (RapidCheck) generate random operation streams and check the
invariants after every operation. RapidCheck shrinks a failure to the smallest
sequence that still reproduces it.

| Invariant | What it rules out |
|---|---|
| Book is never crossed (`best_bid < best_ask`, strictly) | A match the engine failed to make |
| Queues are in submission order | Time priority broken at insert time |
| Trades follow price then time priority | Time priority broken at match time |
| Trades never breach the taker's limit | Executing at a price the taker did not accept |
| Quantity is conserved per order and in aggregate | Shares created or destroyed |
| Every resting order is reachable by id | Ghost orders: resting and matchable, but impossible to cancel |
| Cancelled orders never trade again | Cancellation that does not actually cancel |

**Differential test.** The same operation stream runs through `OrderBook` and
through the deliberately naive `SimpleOrderBook`, and the trade sequences must be
identical. Because both books see the same submissions and produce the same
fills, matching trades also pins every order's resting quantity — it is a full
equivalence check, not just a check on output.

**The tests were verified to fail.** Each was checked against a deliberately
broken build:

| Injected bug | Caught by |
|---|---|
| LIFO instead of FIFO insertion | `QueuesAreInSubmissionOrder`, differential, 2 unit tests |
| Equal prices no longer cross | `BookIsNeverCrossed`, differential, 1 unit test |
| Order left in the id index after its slot is freed | ASan: `heap-use-after-free in OrderBook::cancelOrder` |

The first version of the time-priority property did **not** catch the LIFO bug:
it checked that matching consumes a queue front-first, but never that the queue
is *built* in submission order. `QueuesAreInSubmissionOrder` closes that gap.
That hole would not have been visible from a passing suite.

## Sanitizers and CI

CI runs on every push: Ubuntu × {gcc, clang} and macOS × clang, in Debug and
Release, warnings as errors (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wsign-conversion` and more). A separate job runs the full suite under
**AddressSanitizer + UndefinedBehaviorSanitizer** with `-fno-sanitize-recover`,
so a finding fails the build instead of printing and continuing. Debug builds
also enable standard-library hardening (`_LIBCPP_HARDENING_MODE` /
`_GLIBCXX_ASSERTIONS`), which catches container misuse that the sanitizers do not.

**There is no ThreadSanitizer job.** The engine is single-threaded — there is no
concurrent access anywhere in it — so TSan would verify nothing, and a green
badge would be misleading. It belongs here the day a threaded ingress path
exists.

```bash
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan
```

## Profiling and optimisation

`perf` is Linux-only and gives no PMU counters on Apple Silicon, so profiling
uses **cachegrind and callgrind** in Docker:

```bash
docker build -t orderbook-profile -f tools/profile/Dockerfile tools/profile/
docker run --rm -v "$PWD":/src -w /src orderbook-profile ./tools/profile/run.sh <tag>
```

Two things make the numbers trustworthy:

- **The simulated cache geometry is pinned** (`--I1=32768,8,64 --D1=65536,8,64
  --LL=8388608,16,64`) rather than autodetected. Cachegrind guesses from the host
  CPU, which is unreliable in a container — and an A/B comparison is meaningless
  if the two runs modelled different caches.
- **The workload is identical across platforms.** `std::uniform_int_distribution`
  and `std::shuffle` are specified by distribution, not by exact output, so
  libc++ and libstdc++ generate different order streams from the same seed. The
  benchmark reduces raw generator output by hand instead, so the wall-clock runs
  (macOS) and the cachegrind runs (Linux) measure the same orders.

Cachegrind *simulates* a cache. That makes it deterministic and comparable,
which is what an A/B needs, but it is not wall-clock truth — so every step was
also timed natively.

### Results

Cachegrind, 100k orders, 201 price levels:

| Step | Instructions | Data refs | D1 misses | D1 miss rate |
|---|---:|---:|---:|---:|
| Starting point | 230,364,545 | 108,085,596 | 2,511,600 | 2.3% |
| + order slab | 177,130,394 | 83,702,264 | 1,954,185 | 2.3% |
| + open-addressing id index | 125,942,379 | 54,222,216 | 1,191,936 | 2.2% |
| + pooled level allocator | 136,437,891 | 51,555,252 | 1,175,994 | 2.3% |

Wall clock, 200k orders, best of 9 runs × 3 invocations, Apple M4:

| Step | 201 levels | 20,000 levels |
|---|---:|---:|
| Starting point | 76.96 ns/order | 93.40 ns/order |
| + order slab | 66.20 | 82.60 |
| + open-addressing id index | 56.82 | 77.12 |
| + pooled level allocator | 57.73 | 72.63 |
| **Total** | **−25%** | **−22%** |

**What each step did.**

1. **Order slab.** The profile opened with **38.8% of all instructions inside
   `malloc.c`** — each price level owned a `std::list`, so every resting order
   cost a `malloc` and every fill a `free`. Replacing the per-level lists with
   index-linked nodes in one shared slab removed that allocation entirely.

2. **Open-addressing id index.** With the lists gone, `std::unordered_map` was
   the largest remaining allocator: one node per resting order, one pointer chase
   per lookup. `IdIndex` keeps every entry in one contiguous array and uses
   backward-shift deletion rather than tombstones, since orders are erased
   constantly and tombstones would force repeated rehashing.

3. **Pooled level allocator.** `std::map` still allocated a node per price level,
   and levels churn constantly. A `std::pmr::unsynchronized_pool_resource`
   recycles those blocks. This is a wash at 201 levels but worth ~10% at 20,000,
   which is why the table reports both.

### A rejected optimisation

The obvious next step was to replace the `std::map` level directory with a flat
sorted vector: contiguous, no node allocation, best price at index 0. Cachegrind
liked it — the D1 miss rate improved from 2.2% to 1.9%.

It was still wrong:

| Price levels | `std::map` | Sorted vector |
|---|---:|---:|
| 201 | 57.0 ns/order | 57.6 ns/order |
| 2,001 | 64.9 | 110.0 |
| 20,000 | 76.2 | 483.7 |

Every insert and erase becomes an O(P) memmove, which swamps the locality gain
as soon as the book carries more than a couple of hundred levels — 6.3× slower
at 20,000. It was reverted.

Two things this is worth remembering for: a better cache miss rate is not a
better program, and a change measured only at its default parameters is not
measured. The flat vector looked like a win on both the cachegrind numbers and
the default benchmark band.

### Versus the naive baseline

`SimpleOrderBook` stores both sides in `std::vector` and linearly scans for the
best crossing price on every fill. 200k orders, 201 levels:

```
Implementation         |   best(ms) |    avg(ms) |  avg(ns/order)
-----------------------+------------+------------+---------------
improved OrderBook     |      11.39 |      11.89 |          56.95
Simple baseline        |    2404.99 |    2419.82 |       12024.97
```

The gap is asymptotic, not constant: the baseline is O(N) in resting orders per
fill where this engine is O(log P) in price levels, so it widens as the book
grows. The benchmark also cross-checks that both books execute the same number
of trades, and exits non-zero if they disagree.

## Layout

```
include/OrderBook.h        matching engine
include/IdIndex.h          open-addressing order id -> slab slot map
include/Order.h  Trade.h  Types.h  Enums.h
benchmark/                 naive baseline + benchmark harness
tests/                     unit, property, and differential tests
tools/profile/             valgrind Docker image + profiling script
```

`OrderType` (`FillAndKill`, `Market`) is declared in `Enums.h` but **not
implemented** — `Order` carries no type field, and every order is treated as a
good-till-cancelled limit order.

`OrderBook` is non-copyable and non-movable: its level maps allocate from a pool
held inside the book, so a copy would leave them pointing at the original's
memory resource.
