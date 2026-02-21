#pragma once

enum class Side {
    Buy,
    Sell
};

enum class OrderType{
    GoodTillCancel, // keep in OB unless cancled
    FillAndKill, // fill whatever is possible then cancle
    Market // fill at market price 
};
