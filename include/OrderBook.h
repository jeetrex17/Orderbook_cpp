#pragma once
#include <map>
#include <list>
#include "Enums.h"
#include "Order.h"
#include "Types.h"

class OrderBook{
    private:
        std::map<Price , std::list<Order>> ask;                             // buyser Higer lowest to highest
        std::map<Price , std::list<Order> , std::greater<Price>> bid;       // sellers Hihgerst price to lowerst 

    public:
        void addOrder(Order& order){
            if(order.side == Side::Buy){
                bid[order.price].push_back(order);
            }
            else{
                ask[order.price].push_back(order);
            }
        } 
};
