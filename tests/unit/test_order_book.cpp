#include "lockstep/engine/order_book.hpp"
#include <iostream>
#include <cassert>

namespace {

lockstep::OrderBook::Config makeTestConfig() {
    lockstep::OrderBook::Config config;
    config.instrumentId = 1;
    config.minPrice = 100;
    config.maxPrice = 200;
    config.tickSize = 1;
    config.maxOrders = 1000;
    return config;
}

void testNewOrder() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    lockstep::Order order;
    order.clientId = 1;
    order.orderId = 1;
    order.instrumentId = 1;
    order.side = lockstep::Side::Buy;
    order.price = 150;
    order.quantity = 100;
    
    auto result = book.newOrder(order);
    assert(result.success);
    assert(book.orderCount() == 1);
    assert(book.bestBid() == 150);
    
    std::cout << "  [PASS] Order book new order\n";
}

void testMatching() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    lockstep::Order buy;
    buy.clientId = 1;
    buy.orderId = 1;
    buy.side = lockstep::Side::Buy;
    buy.price = 150;
    buy.quantity = 100;
    book.newOrder(buy);
    
    lockstep::Order sell;
    sell.clientId = 2;
    sell.orderId = 2;
    sell.side = lockstep::Side::Sell;
    sell.price = 140;
    sell.quantity = 50;
    
    auto result = book.newOrder(sell);
    assert(result.success);
    assert(result.filledQuantity == 50);
    assert(!result.matches.empty());
    assert(book.orderCount() == 1);
    
    std::cout << "  [PASS] Order book matching\n";
}

void testCancel() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    lockstep::Order order;
    order.clientId = 1;
    order.orderId = 1;
    order.side = lockstep::Side::Buy;
    order.price = 150;
    order.quantity = 100;
    book.newOrder(order);
    
    auto result = book.cancelOrder(1, 1);
    assert(result.success);
    assert(book.orderCount() == 0);
    
    std::cout << "  [PASS] Order book cancel\n";
}

void testReplace() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    lockstep::Order order;
    order.clientId = 1;
    order.orderId = 1;
    order.side = lockstep::Side::Buy;
    order.price = 150;
    order.quantity = 100;
    book.newOrder(order);
    
    auto result = book.replaceOrder(1, 1, 2, 160, 50);
    assert(result.success);
    
    auto found = book.findOrder(1, 2);
    assert(found.has_value());
    (void)found;
    
    std::cout << "  [PASS] Order book replace\n";
}

void testCrossedBook() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    lockstep::Order buy;
    buy.clientId = 1;
    buy.orderId = 1;
    buy.side = lockstep::Side::Buy;
    buy.price = 150;
    buy.quantity = 100;
    book.newOrder(buy);
    
    lockstep::Order sell;
    sell.clientId = 2;
    sell.orderId = 2;
    sell.side = lockstep::Side::Sell;
    sell.price = 140;
    sell.quantity = 100;
    
    auto result = book.newOrder(sell);
    assert(result.success);
    assert(result.filledQuantity == 100);
    
    std::string error;
    assert(book.checkInvariants(error));
    
    std::cout << "  [PASS] Order book not crossed\n";
}

}

int runOrderBookTests() {
    testNewOrder();
    testMatching();
    testCancel();
    testReplace();
    testCrossedBook();
    return 0;
}
