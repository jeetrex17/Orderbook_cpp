#pragma once

#include "OrderBook.h"

#include <rapidcheck.h>

#include <cstdint>
#include <ostream>

// A generated operation on the book.
//
// Order ids are NOT generated -- the harness assigns them sequentially so they
// are unique by construction. Duplicate-id handling is a separate concern with
// its own dedicated test in test_cancel.cpp, and mixing it in here would make
// every other property depend on it.
struct RawOp {
  bool     is_cancel;
  uint32_t cancel_pick; // reduced modulo the submitted-order count
  uint64_t price;
  uint64_t qty;
  bool     is_buy;
};

// Price band and quantity range are deliberately narrow. Orders have to cross
// often, or the generator spends its budget building a book that never matches
// and the interesting paths go untested.
inline constexpr Price    kPriceLo = 95;
inline constexpr Price    kPriceHi = 106; // exclusive
inline constexpr Quantity kQtyLo   = 1;
inline constexpr Quantity kQtyHi   = 51;  // exclusive

inline std::ostream& operator<<(std::ostream& os, const RawOp& op) {
  if (op.is_cancel) {
    return os << "cancel(pick=" << op.cancel_pick << ")";
  }
  return os << "submit(" << (op.is_buy ? "buy" : "sell") << " " << op.qty
            << " @ " << op.price << ")";
}

namespace rc {

template <>
struct Arbitrary<RawOp> {
  static Gen<RawOp> arbitrary() {
    return gen::build<RawOp>(
        // Cancels are the minority: too many and the book drains faster than
        // it fills, so matching rarely happens.
        gen::set(&RawOp::is_cancel,
                 gen::weightedElement<bool>({{1, true}, {4, false}})),
        gen::set(&RawOp::cancel_pick, gen::arbitrary<uint32_t>()),
        gen::set(&RawOp::price, gen::inRange<uint64_t>(kPriceLo, kPriceHi)),
        gen::set(&RawOp::qty, gen::inRange<uint64_t>(kQtyLo, kQtyHi)),
        gen::set(&RawOp::is_buy, gen::arbitrary<bool>()));
  }
};

} // namespace rc
