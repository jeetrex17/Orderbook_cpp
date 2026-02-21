#pragma once

#include <iostream>
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

        void Print() const {
            std::cout << "-------------- ASKS --------------- \n"; 
            for(const auto& [price , orders] : ask){
                std::cout << "Price: " << price << " | Orders in queue: " << orders.size() << "\n";
            }

            std::cout << "-------------- BIDS --------------- \n"; 
            for(const auto& [price , orders] : bid){
                std::cout << "Price: " << price << " | Orders in queue: " << orders.size() << "\n";
            }
    }
};
