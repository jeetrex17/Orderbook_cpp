#pragma once

#include "Types.h"

// A single execution: the incoming (taker) order matched against a resting
// (maker) order. Trades always execute at the maker's price, since the maker
// was on the book first and set the terms.
struct Trade {
    OrderId  taker_id;
    OrderId  maker_id;
    Price    price;
    Quantity qty;
};

inline bool operator==(const Trade& a, const Trade& b) {
    return a.taker_id == b.taker_id
        && a.maker_id == b.maker_id
        && a.price    == b.price
        && a.qty      == b.qty;
}

inline bool operator!=(const Trade& a, const Trade& b) { return !(a == b); }
