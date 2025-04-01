#include "lockstep/engine/matching_engine.hpp"
#include <iostream>
#include <cassert>

namespace {

lockstep::MatchingEngine::Config makeTestConfig() {
    lockstep::MatchingEngine::Config config;
    
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.tickSize = 1;
    instr.maxOrdersPerLevel = 100;  // Reasonable for tests
    instr.maxPriceLevels = 101;      // 101 levels for 100-200 range
    
    config.instruments.push_back(instr);
    config.useReferenceBook = true;
    
    return config;
}

void testNewOrder() {
    auto config = makeTestConfig();
    lockstep::MatchingEngine engine(config);
    
    lockstep::Order order;
    order.clientId = 1;
    order.orderId = 1;
    order.instrumentId = 1;
    order.side = lockstep::Side::Buy;
    order.price = 150;
    order.quantity = 100;
    
    auto result = engine.newOrder(order);
    assert(result.success);
    assert(engine.totalOrderCount() == 1);
    
    std::cout << "  [PASS] Matching engine new order\n";
}

void testSequencing() {
    auto config = makeTestConfig();
    lockstep::MatchingEngine engine(config);
    
    lockstep::Order order;
    order.clientId = 1;
    order.instrumentId = 1;
    order.side = lockstep::Side::Buy;
    order.price = 150;
    order.quantity = 100;
    
    auto r1 = engine.newOrder(order);
    order.orderId = 2;
    auto r2 = engine.newOrder(order);
    
    assert(r2.commandSeq > r1.commandSeq);
    
    std::cout << "  [PASS] Matching engine sequencing\n";
}

void testDifferential() {
    auto config = makeTestConfig();
    lockstep::MatchingEngine engine(config);
    
    for (int i = 0; i < 10; ++i) {
        lockstep::Order order;
        order.clientId = 1;
        order.orderId = static_cast<lockstep::OrderId>(i + 1);
        order.instrumentId = 1;
        order.side = (i % 2 == 0) ? lockstep::Side::Buy : lockstep::Side::Sell;
        order.price = 150 + (i % 5);
        order.quantity = static_cast<lockstep::Quantity>(10 + i);
        
        engine.newOrder(order);
    }
    
    std::string error;
    assert(engine.verifyAgainstReference(error));
    
    std::cout << "  [PASS] Matching engine differential\n";
}

void testInvariants() {
    auto config = makeTestConfig();
    lockstep::MatchingEngine engine(config);
    
    lockstep::Order order;
    order.clientId = 1;
    order.orderId = 1;
    order.instrumentId = 1;
    order.side = lockstep::Side::Buy;
    order.price = 150;
    order.quantity = 100;
    
    engine.newOrder(order);
    
    std::string error;
    assert(engine.checkInvariants(error));
    
    std::cout << "  [PASS] Matching engine invariants\n";
}

void testKillSwitch() {
    auto config = makeTestConfig();
    lockstep::MatchingEngine engine(config);
    
    engine.setKillSwitch(true);
    
    lockstep::Order order;
    order.clientId = 1;
    order.orderId = 1;
    order.instrumentId = 1;
    order.side = lockstep::Side::Buy;
    order.price = 150;
    order.quantity = 100;
    
    auto result = engine.newOrder(order);
    assert(!result.success);
    assert(result.reason == lockstep::RejectionReason::KillSwitchActive);
    
    std::cout << "  [PASS] Matching engine kill switch\n";
}

}

int runMatchingEngineTests() {
    std::cout << "Running matching engine tests...\n";
    testNewOrder();
    testSequencing();
    testDifferential();
    testInvariants();
    testKillSwitch();
    return 0;
}
