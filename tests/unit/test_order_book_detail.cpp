#include "lockstep/engine/order_book.hpp"
#include <iostream>
#include <cassert>
#include <random>
#include <vector>

namespace {

lockstep::OrderBook::Config makeTestConfig() {
    lockstep::OrderBook::Config config;
    config.instrumentId = 1;
    config.minPrice = 100;
    config.maxPrice = 200;
    config.tickSize = 1;
    config.maxOrders = 10000;  // Reasonable limit for tests
    return config;
}

void testNewResting() {
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
    assert(book.bestBid() == 150);
    
    std::cout << "  [PASS] New resting order\n";
}

void testIOC() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    lockstep::Order resting;
    resting.clientId = 1;
    resting.orderId = 1;
    resting.instrumentId = 1;
    resting.side = lockstep::Side::Buy;
    resting.price = 150;
    resting.quantity = 100;
    book.newOrder(resting);
    
    lockstep::Order ioc;
    ioc.clientId = 2;
    ioc.orderId = 2;
    ioc.instrumentId = 1;
    ioc.side = lockstep::Side::Sell;
    ioc.price = 150;
    ioc.quantity = 200;
    ioc.tif = lockstep::TimeInForce::IOC;
    
    auto result = book.newOrder(ioc);
    assert(result.success);
    assert(result.filledQuantity == 100);
    assert(book.orderCount() == 0);
    
    std::cout << "  [PASS] IOC order\n";
}

void testFOK() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    lockstep::Order resting;
    resting.clientId = 1;
    resting.orderId = 1;
    resting.instrumentId = 1;
    resting.side = lockstep::Side::Buy;
    resting.price = 150;
    resting.quantity = 100;
    book.newOrder(resting);
    
    lockstep::Order fok1;
    fok1.clientId = 2;
    fok1.orderId = 2;
    fok1.instrumentId = 1;
    fok1.side = lockstep::Side::Sell;
    fok1.price = 150;
    fok1.quantity = 100;
    fok1.tif = lockstep::TimeInForce::FOK;
    
    auto result1 = book.newOrder(fok1);
    assert(result1.success);
    assert(result1.filledQuantity == 100);
    
    resting.orderId = 3;
    book.newOrder(resting);
    
    lockstep::Order fok2;
    fok2.clientId = 2;
    fok2.orderId = 4;
    fok2.instrumentId = 1;
    fok2.side = lockstep::Side::Sell;
    fok2.price = 150;
    fok2.quantity = 200;
    fok2.tif = lockstep::TimeInForce::FOK;
    
    auto result2 = book.newOrder(fok2);
    assert(!result2.success);
    assert(result2.reason == lockstep::RejectionReason::FOKCannotFill);
    
    std::cout << "  [PASS] FOK order\n";
}

void testPriority() {
    auto config = makeTestConfig();
    lockstep::OrderBook book(config);
    
    for (int i = 0; i < 5; ++i) {
        lockstep::Order order;
        order.clientId = 1;
        order.orderId = static_cast<lockstep::OrderId>(i + 1);
        order.instrumentId = 1;
        order.side = lockstep::Side::Buy;
        order.price = 150;
        order.quantity = 100;
        book.newOrder(order);
    }
    
    lockstep::Order sell;
    sell.clientId = 2;
    sell.orderId = 10;
    sell.instrumentId = 1;
    sell.side = lockstep::Side::Sell;
    sell.price = 150;
    sell.quantity = 100;
    
    auto result = book.newOrder(sell);
    assert(result.success);
    assert(result.matches[0].passiveOrderId == 1);
    
    std::cout << "  [PASS] Priority ordering\n";
}

} // namespace

int runOrderBookDetailTests() {
    testNewResting();
    testIOC();
    testFOK();
    testPriority();
    return 0;
}

