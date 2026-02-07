#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <list>
#include <map>

enum class OrderType {
    GoodTillCancle,
    FillAndKill
};

enum class Side {
    buy,
    sell,
};

using Price = std::int32_t;
using Quantity = std::uint32_t; // uint32_t because quanity cant be -ve;
using OrderId = std::uint64_t ;


struct LevelInfo {
    Price piece_;
    Quantity quanity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos {
public:
    OrderBookLevelInfos(const LevelInfos& bids, const LevelInfos& asks)
        : bids_{bids} // Member Initializer List: Direct construction using Uniform Initialization
        , asks_{asks}
    { } // Constructor body: Empty because members are already initialized above.

    const LevelInfos& GetBids() const { return bids_; }
    const LevelInfos& GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};

class Order
{
    public:
        Order(OrderType orderType , OrderId orderId , Side side , Price price , Quantity quanity)
            : orderType_ { orderType }
            , orderId_ { orderId }
            , side_ { side }
            , price_ { price }
            , initalQuantity_ { quanity }
            , remainingQuantity_ { quanity }
        { }

        OrderId GetOrderId() const {return orderId_ ;}
        Side GetSide() const {return side_;}
        Price GetPrice() const { return price_;}
        OrderType GetOrderType() const { return orderType_;}
        Quantity GetInitialQuantity() const { return initalQuantity_;}
        Quantity GetRemainingQuantity() const { return remainingQuantity_;}
        Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity();}

        void Fill(Quantity quanity){
            if(quanity > GetRemainingQuantity()){
                throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity." , GetOrderId()));
            }

            remainingQuantity_ -= quanity;
        }

    private:
        OrderType orderType_;
        OrderId orderId_;
        Side side_;
        Price price_;
        Quantity initalQuantity_;
        Quantity remainingQuantity_;

};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify
{
    public:
        OrderModify(OrderId orderId , Side side , Price price , Quantity quantity)
            : orderId_ { orderId }
            , price_ { price }
            , side_ { side }
            , quantity_ { quantity }
        { }

        OrderId GetOrderId() const {return orderId_ ;}
        Price GetPrice() const { return price_ ;}
        Side GetSide() const { return side_ ;}
        Quantity GetQuantity() const { return quantity_ ;}

        OrderPointer ToOrderPointer(OrderType type) const{
            return std::make_shared<Order>(type  , GetOrderId() , GetSide() , GetPrice() , GetQuantity());
        }

    private:
        OrderId orderId_;
        Side side_;
        Price price_;
        Quantity quantity_;


};

struct TradeInfo 
{
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade 
{
    public:
        Trade(const TradeInfo& bidTrade , const TradeInfo& askTrade)
            : bidTrade_ { bidTrade }
            , askTrade_ { askTrade }
        { }

        const TradeInfo& getBidTrade() const { return bidTrade_;}
        const TradeInfo& getAsktrade() const { return askTrade_;}

    private:
        TradeInfo bidTrade_;
        TradeInfo askTrade_;

};

using Trades = std::vector<Trade> ;

class Orderbook
{
    private:
        struct OrderEntry
        {
            OrderPointer order_ { nullptr };
            OrderPointers::iterator location_ ;
        };

        std::map<Price , OrderPointers , std::greater<Price>> bids_;
        std::map<Price , OrderPointers , std::less<Price>> asks_; 
        std::unordered_map<OrderId, OrderEntry> orders_;

        bool CanMatch(Side side , Price price){
            if(side == Side::buy){
                if (asks_.empty()){
                    return false;
                }

                const auto& [bestAsk, _] = *asks_.begin();

                return price >- bestAsk;
            } 
            else {
                if(bids_.empty()){
                    return false;
                }

                const auto& [bestBid , _] = *bids_.begin();
                return price <= bestBid;
            }
        }

};

int main(){

}
