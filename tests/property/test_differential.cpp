#include <cassert>
#include <cstdlib>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>
#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/engine/reference_book.hpp"
#include "lockstep/risk/risk_engine.hpp"

#define TEST_ASSERT(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)

#include "../../bench/campaign_support.hpp"
#include "lockstep/engine/reference_model.hpp"

namespace {
using lockstep::IndependentReferenceModel;
using lockstep::RefClientExposure;

lockstep::MatchingEngine::Config makeConfig() {
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

void testSeededMixedWorkload() {
    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;
    IndependentReferenceModel ref;
    ref.addInstrument(config.instruments[0]);

    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<uint32_t> clientDist(1, 4);
    std::uniform_int_distribution<int64_t> priceDist(140, 160);
    std::uniform_int_distribution<uint32_t> qtyDist(1, 20);
    std::uniform_int_distribution<uint32_t> opDist(0, 3);  // 0,1: NewOrder, 2: Cancel, 3: IOC/FOK

    std::vector<std::pair<lockstep::ClientId, lockstep::OrderId>> activeOrders;

    uint64_t nextOrderId = 1;
    for (int step = 0; step < 300; ++step) {
        uint32_t op = opDist(rng);
        if (op <= 1) {
            // New GTC Order
            lockstep::Order o;
            o.clientId = clientDist(rng);
            o.orderId = nextOrderId++;
            o.instrumentId = 1;
            o.side = (rng() % 2 == 0) ? lockstep::Side::Buy : lockstep::Side::Sell;
            o.price = priceDist(rng);
            o.quantity = qtyDist(rng);
            o.tif = lockstep::TimeInForce::GTC;

            auto rReal = engine.newOrder(o);
            auto rRef = ref.newOrder(o);

            TEST_ASSERT(rReal.success == rRef.success);
            TEST_ASSERT(rReal.reason == rRef.reason);
            TEST_ASSERT(rReal.matches.size() == rRef.matches.size());
            for (size_t m = 0; m < rReal.matches.size(); ++m) {
                TEST_ASSERT(rReal.matches[m].price == rRef.matches[m].price);
                TEST_ASSERT(rReal.matches[m].quantity == rRef.matches[m].quantity);
                TEST_ASSERT(rReal.matches[m].passiveOrderId == rRef.matches[m].passiveOrderId);
                TEST_ASSERT(rReal.matches[m].aggressiveOrderId ==
                            rRef.matches[m].aggressiveOrderId);
            }

            if (rReal.success) {
                activeOrders.push_back({o.clientId, o.orderId});
            }
        } else if (op == 2 && !activeOrders.empty()) {
            // Cancel random order
            size_t idx = rng() % activeOrders.size();
            auto [cid, oid] = activeOrders[idx];

            auto rReal = engine.cancelOrder(cid, oid, 1);
            auto rRef = ref.cancelOrder(cid, oid, 1);

            TEST_ASSERT(rReal.success == rRef.success);
            TEST_ASSERT(rReal.reason == rRef.reason);

            activeOrders.erase(activeOrders.begin() + static_cast<std::ptrdiff_t>(idx));
        } else {
            // IOC or FOK Order
            lockstep::Order o;
            o.clientId = clientDist(rng);
            o.orderId = nextOrderId++;
            o.instrumentId = 1;
            o.side = (rng() % 2 == 0) ? lockstep::Side::Buy : lockstep::Side::Sell;
            o.price = priceDist(rng);
            o.quantity = qtyDist(rng);
            o.tif = (rng() % 2 == 0) ? lockstep::TimeInForce::IOC : lockstep::TimeInForce::FOK;

            auto rReal = engine.newOrder(o);
            auto rRef = ref.newOrder(o);

            TEST_ASSERT(rReal.success == rRef.success);
            TEST_ASSERT(rReal.reason == rRef.reason);
            TEST_ASSERT(rReal.matches.size() == rRef.matches.size());
        }

        // Assert invariant state between engine and reference
        auto* realBook = engine.getBook(1);
        auto* refBook = ref.getBook(1);
        TEST_ASSERT(realBook != nullptr);
        TEST_ASSERT(refBook != nullptr);
        TEST_ASSERT(realBook->orderCount() == refBook->orderCount());
        TEST_ASSERT(realBook->bestBid() == refBook->bestBid());
        TEST_ASSERT(realBook->bestAsk() == refBook->bestAsk());
        TEST_ASSERT(realBook->totalBidQuantity() == refBook->totalBidQuantity());
        TEST_ASSERT(realBook->totalAskQuantity() == refBook->totalAskQuantity());
    }

    std::cout << "  [PASS] Seeded mixed workload\n";
}

void testCapacityExhaustion() {
    lockstep::MatchingEngine::Config config;
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.tickSize = 1;
    instr.maxOrdersPerLevel = 2;
    instr.maxPriceLevels = 2;  // Total capacity = 4 orders
    config.instruments.push_back(instr);
    config.useReferenceBook = true;

    lockstep::MatchingEngine engine(config);
    IndependentReferenceModel ref;
    ref.addInstrument(instr);

    for (int i = 1; i <= 6; ++i) {
        lockstep::Order o;
        o.clientId = 1;
        o.orderId = static_cast<lockstep::OrderId>(i);
        o.instrumentId = 1;
        o.side = lockstep::Side::Buy;
        o.price = 100;
        o.quantity = 10;
        o.tif = lockstep::TimeInForce::GTC;

        auto rReal = engine.newOrder(o);
        auto rRef = ref.newOrder(o);

        TEST_ASSERT(rReal.success == rRef.success);
        TEST_ASSERT(rReal.reason == rRef.reason);
        if (i <= 4) {
            TEST_ASSERT(rReal.success);
        } else {
            TEST_ASSERT(!rReal.success);
            TEST_ASSERT(rReal.reason == lockstep::RejectionReason::CapacityExceeded);
        }
    }

    std::cout << "  [PASS] Capacity exhaustion\n";
}

void testInvalidIdsAndInputs() {
    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    IndependentReferenceModel ref;
    ref.addInstrument(config.instruments[0]);

    // Unknown instrument
    {
        lockstep::Order o;
        o.clientId = 1;
        o.orderId = 1;
        o.instrumentId = 999;
        o.side = lockstep::Side::Buy;
        o.price = 150;
        o.quantity = 10;
        auto rReal = engine.newOrder(o);
        auto rRef = ref.newOrder(o);
        TEST_ASSERT(!rReal.success && rReal.reason == lockstep::RejectionReason::UnknownInstrument);
        TEST_ASSERT(!rRef.success && rRef.reason == lockstep::RejectionReason::UnknownInstrument);
    }

    // Invalid price (below min)
    {
        lockstep::Order o;
        o.clientId = 1;
        o.orderId = 1;
        o.instrumentId = 1;
        o.price = 50;  // min is 100
        o.quantity = 10;
        auto rReal = engine.newOrder(o);
        auto rRef = ref.newOrder(o);
        TEST_ASSERT(!rReal.success && rReal.reason == lockstep::RejectionReason::InvalidPrice);
        TEST_ASSERT(!rRef.success && rRef.reason == lockstep::RejectionReason::InvalidPrice);
    }

    // Invalid price (above max)
    {
        lockstep::Order o;
        o.clientId = 1;
        o.orderId = 1;
        o.instrumentId = 1;
        o.price = 250;  // max is 200
        o.quantity = 10;
        auto rReal = engine.newOrder(o);
        auto rRef = ref.newOrder(o);
        TEST_ASSERT(!rReal.success && rReal.reason == lockstep::RejectionReason::InvalidPrice);
        TEST_ASSERT(!rRef.success && rRef.reason == lockstep::RejectionReason::InvalidPrice);
    }

    // Zero quantity
    {
        lockstep::Order o;
        o.clientId = 1;
        o.orderId = 1;
        o.instrumentId = 1;
        o.price = 150;
        o.quantity = 0;
        auto rReal = engine.newOrder(o);
        auto rRef = ref.newOrder(o);
        TEST_ASSERT(!rReal.success && rReal.reason == lockstep::RejectionReason::InvalidQuantity);
        TEST_ASSERT(!rRef.success && rRef.reason == lockstep::RejectionReason::InvalidQuantity);
    }

    // Duplicate OrderId
    {
        lockstep::Order o;
        o.clientId = 1;
        o.orderId = 42;
        o.instrumentId = 1;
        o.price = 150;
        o.quantity = 10;
        TEST_ASSERT(engine.newOrder(o).success);
        TEST_ASSERT(ref.newOrder(o).success);

        auto rReal = engine.newOrder(o);
        auto rRef = ref.newOrder(o);
        TEST_ASSERT(!rReal.success && rReal.reason == lockstep::RejectionReason::DuplicateOrderId);
        TEST_ASSERT(!rRef.success && rRef.reason == lockstep::RejectionReason::DuplicateOrderId);
    }

    // Cancel non-existent order
    {
        auto rReal = engine.cancelOrder(1, 9999, 1);
        auto rRef = ref.cancelOrder(1, 9999, 1);
        TEST_ASSERT(!rReal.success && rReal.reason == lockstep::RejectionReason::OrderNotFound);
        TEST_ASSERT(!rRef.success && rRef.reason == lockstep::RejectionReason::OrderNotFound);
    }

    std::cout << "  [PASS] Invalid IDs and inputs\n";
}

void testAllOrderPolicies() {
    auto config = makeConfig();
    lockstep::MatchingEngine engine(config);
    IndependentReferenceModel ref;
    ref.addInstrument(config.instruments[0]);

    // 1. Resting buy 100 @ 150
    lockstep::Order b1;
    b1.clientId = 1;
    b1.orderId = 1;
    b1.instrumentId = 1;
    b1.side = lockstep::Side::Buy;
    b1.price = 150;
    b1.quantity = 100;
    b1.tif = lockstep::TimeInForce::GTC;
    TEST_ASSERT(engine.newOrder(b1).success);
    TEST_ASSERT(ref.newOrder(b1).success);

    // 2. FOK order that cannot fill (needs 150 lots, only 100 available)
    lockstep::Order fok1;
    fok1.clientId = 2;
    fok1.orderId = 2;
    fok1.instrumentId = 1;
    fok1.side = lockstep::Side::Sell;
    fok1.price = 150;
    fok1.quantity = 150;
    fok1.tif = lockstep::TimeInForce::FOK;
    auto rFok1 = engine.newOrder(fok1);
    auto rFok1Ref = ref.newOrder(fok1);
    TEST_ASSERT(!rFok1.success && rFok1.reason == lockstep::RejectionReason::FOKCannotFill);
    TEST_ASSERT(!rFok1Ref.success && rFok1Ref.reason == lockstep::RejectionReason::FOKCannotFill);
    // Ensure book is untouched: b1 still has 100 lots
    TEST_ASSERT(engine.getBook(1)->bidQuantity(150) == 100);
    TEST_ASSERT(ref.getBook(1)->bidQuantity(150) == 100);

    // 3. IOC order that partially fills 40 lots, remaining 60 cancelled (no resting)
    lockstep::Order ioc1;
    ioc1.clientId = 2;
    ioc1.orderId = 3;
    ioc1.instrumentId = 1;
    ioc1.side = lockstep::Side::Sell;
    ioc1.price = 150;
    ioc1.quantity = 40;
    ioc1.tif = lockstep::TimeInForce::IOC;
    auto rIoc1 = engine.newOrder(ioc1);
    auto rIoc1Ref = ref.newOrder(ioc1);
    TEST_ASSERT(rIoc1.success && rIoc1Ref.success);
    TEST_ASSERT(rIoc1.matches.size() == 1 && rIoc1.matches[0].quantity == 40);
    TEST_ASSERT(rIoc1Ref.matches.size() == 1 && rIoc1Ref.matches[0].quantity == 40);
    TEST_ASSERT(engine.getBook(1)->bidQuantity(150) == 60);
    TEST_ASSERT(ref.getBook(1)->bidQuantity(150) == 60);

    // 4. Self-trade prevention: Client 1 cannot match against Client 1
    lockstep::Order st;
    st.clientId = 1;
    st.orderId = 4;
    st.instrumentId = 1;
    st.side = lockstep::Side::Sell;
    st.price = 150;
    st.quantity = 10;
    st.tif = lockstep::TimeInForce::GTC;
    auto rSt = engine.newOrder(st);
    auto rStRef = ref.newOrder(st);
    TEST_ASSERT(rSt.matches.empty() && rStRef.matches.empty());

    std::cout << "  [PASS] All order policies (GTC, IOC, FOK, STP)\n";
}

void testFullIndependentCommandState() {
    for (uint64_t seed = 1; seed <= 500; ++seed) {
        auto specs = campaign::commands(seed);
        lockstep::MatchingEngine engine(campaign::config());
        lockstep::RiskEngine risk;
        std::vector<lockstep::Match> events;
        for (size_t i = 0; i < specs.size(); ++i) {
            lockstep::MatchingEngine::Result result;
            std::string error;
            TEST_ASSERT(lockstep::RecoveryManager::applyRecord(engine, risk, specs[i].record, error,
                                                               &result));
            events.insert(events.end(), result.matches.begin(), result.matches.end());
            auto actual = campaign::observed(engine, risk, events);
            auto expected = campaign::expected(specs, i + 1, 0);
            if (actual != expected)
                std::cerr << "seed=" << seed << " command=" << i + 1 << "\nactual:\n"
                          << actual << "expected:\n"
                          << expected;
            TEST_ASSERT(actual == expected);
        }
    }
    std::cout
        << "  [PASS] 12000 independently compared mixed commands, complete FIFO/risk/trade state\n";
}

}  // namespace

int runDifferentialTests() {
    testSeededMixedWorkload();
    testFullIndependentCommandState();
    testCapacityExhaustion();
    testInvalidIdsAndInputs();
    testAllOrderPolicies();
    return 0;
}
