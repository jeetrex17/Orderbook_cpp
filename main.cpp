#include "include/OrderBook.h"

#include <iostream>
#include <vector>

namespace {

// The book no longer prints anything during matching -- it returns executions
// as data. Printing is the demo's job.
void submit(OrderBook& book, const Order& order) {
    std::vector<Trade> trades;
    book.processOrder(order, trades);
    for (const Trade& t : trades) {
        std::cout << "TRADE: " << t.qty << " shares @ " << t.price
                  << " (taker " << t.taker_id << " / maker " << t.maker_id << ")\n";
    }
}

} // namespace

int main() {
    OrderBook book;

    // Prices are integer ticks, not currency: Price is uint64_t.
    std::cout << "=== Simple cross (buy then sell at same price) ===\n";
    submit(book, Order(1001, 1500, Side::Buy,  100));
    submit(book, Order(1002, 1500, Side::Sell,  60));
    book.Print();

    std::cout << "\n=== Add more bids below & above ===\n";
    submit(book, Order(1003, 1490, Side::Buy,  200));
    submit(book, Order(1004, 1495, Side::Buy,  150));
    submit(book, Order(1005, 1510, Side::Buy,   80));
    book.Print();

    std::cout << "\n=== Sell that should cross multiple bid levels ===\n";
    submit(book, Order(2001, 1492, Side::Sell, 400));
    book.Print();

    std::cout << "\n=== Add asks (offers to sell) ===\n";
    submit(book, Order(3001, 1515, Side::Sell, 120));
    submit(book, Order(3002, 1520, Side::Sell, 250));
    submit(book, Order(3003, 1505, Side::Sell, 300));
    book.Print();

    std::cout << "\n=== Big buy that crosses multiple ask levels ===\n";
    submit(book, Order(4001, 1530, Side::Buy, 500));
    book.Print();

    std::cout << "\n=== Add limit order that doesn't cross ===\n";
    submit(book, Order(5001, 1498, Side::Buy,  180));
    submit(book, Order(5002, 1525, Side::Sell, 100));
    book.Print();

    std::cout << "\n=== Partial fill + remainder on book ===\n";
    submit(book, Order(6001, 1518, Side::Buy,  80));
    book.Print();

    return 0;
}
