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
        assert(init_qty > 0);
        assert(rem_qty <= init_qty);
        assert(rem_qty >= 0);
    }
};
