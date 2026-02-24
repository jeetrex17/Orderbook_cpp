#pragma once

#include "Enums.h"
#include "Order.h"
#include "Types.h"
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <unordered_map>

class OrderBook {
private:
  std::map<Price, std::list<Order>> asks; // buyser Higer lowest to highest
  std::map<Price, std::list<Order>, std::greater<Price>> bids; // sellers Hihgerst price to lowerst
  std::unordered_map<OrderId, std::list<Order>::iterator> orderPointers; // this is to get O(1) when we do cancleOrder its maps order ids to excate memeory locaitons of the orders 
public:
  void processOrder(Order newOrder) {

    if (newOrder.side == Side::Buy) {
      while (newOrder.remaining_qty > 0 && !asks.empty()) {
        auto bestAskIter = asks.begin();
        Price bestAskPrice = bestAskIter->first;
        std::list<Order> &askQueue = bestAskIter->second;

        if (bestAskPrice > newOrder.price) { // there is no order we can match 
          break;
        }

        Order &restingAsk = askQueue.front();

        Quantity tradeQty = std::min(restingAsk.remaining_qty , newOrder.remaining_qty);
        newOrder.remaining_qty -= tradeQty;
        restingAsk.remaining_qty -= tradeQty;

        std::cout << "TRADE: " << tradeQty << " shares @ " << bestAskPrice
                  << "\n";

        //If the resting ask is fully filled, remove it from the queue
        if (restingAsk.remaining_qty == 0) {
          orderPointers.erase(restingAsk.orderid);
          askQueue.pop_front();
        }

        // If the queue for this price is now empty, delete the price level from the map
        if (askQueue.empty()) {
          asks.erase(bestAskIter);
        }
      }

      // After all matching is done, if the incoming order still has shares, add it to the bids map
      if (newOrder.remaining_qty > 0) {
        //bids[newOrder.price].push_back(newOrder);
        auto& queue = bids[newOrder.price];  // getting referance to the list
        auto it = queue.insert(queue.end() , newOrder);
        orderPointers[  newOrder.orderid] = it; // insert returns an iterator to the newly inserted element
      }
    } else {
    while (newOrder.remaining_qty > 0 && !bids.empty()) {
        auto bestBidIter = bids.begin();
        Price bestbidPrice = bestBidIter->first;
        std::list<Order> &bidQueue = bestBidIter->second;

        if (bestbidPrice < newOrder.price) {
          break;
        }

        Order &restingbid = bidQueue.front();

        Quantity tradeQty = std::min(restingbid.remaining_qty , newOrder.remaining_qty);
        newOrder.remaining_qty -= tradeQty;
        restingbid.remaining_qty -= tradeQty;

        std::cout << "TRADE: " << tradeQty << " shares @ " << bestbidPrice << "\n";

        if (restingbid.remaining_qty == 0) {
          orderPointers.erase(restingbid.orderid);   // we have to remove tracking FIRST or there will be use after free
          bidQueue.pop_front();
        }

        if (bidQueue.empty()) {
          bids.erase(bestBidIter);
        }
      }

      if (newOrder.remaining_qty > 0) {
        //asks[newOrder.price].push_back(newOrder);
        auto& queue = asks[newOrder.price];
        auto it = queue.insert(queue.end(), newOrder);
        orderPointers[newOrder.orderid] = it;
      }

    }
  }
void cancelOrder(OrderId id) {
    auto mapIt = orderPointers.find(id);
    if (mapIt == orderPointers.end()) {
        return; 
    }

    auto listIterator = mapIt->second;           // this is std::list<Order>::iterator
    Price price       = listIterator->price;     // the price level where it lives
    Side  side        = listIterator->side;

    if (side == Side::Buy) {
        auto priceIt = bids.find(price);
        if (priceIt != bids.end()) {
            auto& orderList = priceIt->second;
            orderList.erase(listIterator);          

            if (orderList.empty()) {
                bids.erase(priceIt);
            }
        }
    }
    else {  // Side::Sell
        auto priceIt = asks.find(price);
        if (priceIt != asks.end()) {
            auto& orderList = priceIt->second;
            orderList.erase(listIterator);

            if (orderList.empty()) {
                asks.erase(priceIt);
            }
        }
    }

    orderPointers.erase(mapIt);
}
void Print() const {
    std::cout << "--------------------- ASKS ---------------------\n";
    if (asks.empty()) {
        std::cout << " (no asks)\n";
    } else {
        for (const auto& [price, orders] : asks) {
            std::cout << "Price: " << price 
                      << " | Qty: " << orders.size() 
                      << " | Orders in queue: " << orders.size() 
                      << "\n";
        }
    }

    std::cout << "\n--------------------- BIDS ---------------------\n";
    if (bids.empty()) {
        std::cout << " (no bids)\n";
    } else {
        for (const auto& [price, orders] : bids) {
            std::cout << "Price: " << price 
                      << " | Qty: " << orders.size()
                      << " | Orders in queue: " << orders.size() 
                      << "\n";
        }
    }
    std::cout << "-----------------------------------------------\n\n";
}
};
