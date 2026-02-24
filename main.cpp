#include "include/OrderBook.h"


int main() {
    OrderBook book;

    std::cout << "=== Simple cross (buy then sell at same price) ===\n";
    book.processOrder(Order(1001, 1500.0, Side::Buy,  100)); 
    book.processOrder(Order(1002, 1500.0, Side::Sell,  60));  
    book.Print();

    std::cout << "\n=== Add more bids below & above ===\n";
    book.processOrder(Order(1003, 1490.0, Side::Buy,  200));
    book.processOrder(Order(1004, 1495.0, Side::Buy,  150));
    book.processOrder(Order(1005, 1510.0, Side::Buy,   80));
    book.Print();

    std::cout << "\n=== Sell that should cross multiple bid levels ===\n";
    book.processOrder(Order(2001, 1492.0, Side::Sell, 400));
    book.Print();

    std::cout << "\n=== Add asks (offers to sell) ===\n";
    book.processOrder(Order(3001, 1515.0, Side::Sell, 120));
    book.processOrder(Order(3002, 1520.0, Side::Sell, 250));
    book.processOrder(Order(3003, 1505.0, Side::Sell, 300));
    book.Print();

    std::cout << "\n=== Big buy that crosses multiple ask levels ===\n";
    book.processOrder(Order(4001, 1530.0, Side::Buy, 500)); 
    book.Print();

    std::cout << "\n=== Add limit order that doesn't cross ===\n";
    book.processOrder(Order(5001, 1498.0, Side::Buy,  180));
    book.processOrder(Order(5002, 1525.0, Side::Sell, 100));
    book.Print();

    std::cout << "\n=== Partial fill + remainder on book ===\n";
    book.processOrder(Order(6001, 1518.0, Side::Buy,  80));
    book.Print();

    return 0;
}
