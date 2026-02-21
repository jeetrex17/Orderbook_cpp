#include <iostream>
#include "include/OrderBook.h"

int main() {
    OrderBook book;
    Order order1(1, 1500, Side::Buy, 100);
    book.addOrder(order1);

    Order order2(1, 1500, Side::Sell, 100);
    book.addOrder(order2);
    book.Print();

    return 0;
}
