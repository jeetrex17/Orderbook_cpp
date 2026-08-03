#pragma once
#include "Enums.h"
#include "Types.h"
#include <cassert>

struct Order {
    OrderId         orderid;
    Price           price;
    Side            side;
    Quantity        initial_qty;
    Quantity        remaining_qty;

    Order(OrderId oid, Price p, Side s, Quantity init_qty, Quantity rem_qty = 0)
        : orderid(oid),
          price(p),
          side(s),
          initial_qty(init_qty),
          remaining_qty(rem_qty != 0 ? rem_qty : init_qty)
    {
        // Quantity is unsigned, so a ">= 0" check would always hold. Assert on
        // the resolved remaining_qty instead of the raw argument, which is 0
        // in the common "remaining defaults to initial" case.
        assert(init_qty > 0 && "an order must have positive quantity");
        assert(remaining_qty <= initial_qty && "remaining cannot exceed initial");
    }
};
