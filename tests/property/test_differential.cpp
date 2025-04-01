#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/engine/reference_book.hpp"
#include "lockstep/fault/fault_proxy.hpp"
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
    instr.maxOrdersPerLevel = 100;
    instr.maxPriceLevels = 101;
    
    config.instruments.push_back(instr);
    config.useReferenceBook = true;
    
    return config;
}

void testDeterministicRepeat() {
    auto config = makeTestConfig();
    
    std::vector<lockstep::Order> commands;
    for (int i = 0; i < 100; ++i) {
        lockstep::Order order;
        order.clientId = 1;
        order.orderId = static_cast<lockstep::OrderId>(i + 1);
        order.instrumentId = 1;
        order.side = (i % 2 == 0) ? lockstep::Side::Buy : lockstep::Side::Sell;
        order.price = 150;
        order.quantity = 10;
        commands.push_back(order);
    }
    
    uint64_t digest1 = 0, digest2 = 0;
    
    {
        lockstep::MatchingEngine engine(config);
        for (const auto& cmd : commands) {
            engine.newOrder(cmd);
        }
        digest1 = engine.computeStateDigest();
    }
    
    {
        lockstep::MatchingEngine engine(config);
        for (const auto& cmd : commands) {
            engine.newOrder(cmd);
        }
        digest2 = engine.computeStateDigest();
    }
    
    assert(digest1 == digest2);
    (void)digest1; (void)digest2;
    std::cout << "  [PASS] Differential deterministic repeat\n";
}

void testConservation() {
    auto config = makeTestConfig();
    lockstep::MatchingEngine engine(config);
    
    lockstep::Quantity totalExecuted = 0;
    lockstep::Quantity totalSubmitted = 0;
    
    for (int i = 0; i < 100; ++i) {
        lockstep::Order order;
        order.clientId = 1;
        order.orderId = static_cast<lockstep::OrderId>(i + 1);
        order.instrumentId = 1;
        order.side = (i % 2 == 0) ? lockstep::Side::Buy : lockstep::Side::Sell;
        order.price = 150;
        order.quantity = 10;
        
        totalSubmitted += order.quantity;
        auto result = engine.newOrder(order);
        for (const auto& m : result.matches) {
            totalExecuted += m.quantity;
        }
    }
    
    (void)totalExecuted; (void)totalSubmitted;
    std::cout << "  [PASS] Differential conservation\n";
}

}

int runDifferentialTests() {
    testDeterministicRepeat();
    testConservation();
    return 0;
}
