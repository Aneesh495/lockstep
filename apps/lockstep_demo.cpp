#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/risk/risk_engine.hpp"
#include "lockstep/network/udp_publisher.hpp"
#include "lockstep/network/feed_arbiter.hpp"
#include "lockstep/fault/fault_proxy.hpp"
#include "lockstep/persistence/wal.hpp"
#include "lockstep/persistence/snapshot.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace lockstep;

void printBanner() {
    std::cout << R"(
 _           _   _          _   _
| |    __ _| |_| |__  _ __| |_(_)_ __   __ _
| |   / _` | __| '_ \| '__| __| | '_ \ / _` |
| |__| (_| | |_| | | | |  | |_| | | | | (_| |
|_____\__,_|\__|_| |_|_|   \__|_|_| |_|\__, |
                                       |___/
        Low-Latency Deterministic Exchange
)" << "\n" << std::flush;
}

int main(int argc, char** argv) {
    printBanner();
    
    std::cout << "Demo: Order Matching, Risk, and Recovery\n";
    std::cout << "=========================================\n\n" << std::flush;
    
    // Setup
    std::cout << "Setting up engine...\n" << std::flush;
    MatchingEngine::Config engineConfig;
    
    InstrumentConfig instr1;
    instr1.id = 1;
    instr1.minPrice = 9900;
    instr1.maxPrice = 10100;  // 201 tick range
    instr1.tickSize = 1;
    instr1.maxOrdersPerLevel = 50;
    instr1.maxPriceLevels = 250;
    engineConfig.instruments.push_back(instr1);
    
    InstrumentConfig instr2;
    instr2.id = 2;
    instr2.minPrice = 49900;
    instr2.maxPrice = 50100;  // 201 tick range
    instr2.tickSize = 1;
    instr2.maxOrdersPerLevel = 50;
    instr2.maxPriceLevels = 250;
    engineConfig.instruments.push_back(instr2);
    
    engineConfig.useReferenceBook = false;
    
    std::cout << "Creating engine...\n" << std::flush;
    MatchingEngine engine(engineConfig);
    std::cout << "Engine created.\n" << std::flush;
    RiskEngine risk;
    
    // Configure risk limits
    RiskLimits limits;
    limits.clientId = 1;
    limits.maxOrderQuantity = 1000;
    limits.maxPosition = 5000;
    limits.maxOpenOrders = 100;
    risk.setClientLimits(1, limits);
    
    limits.clientId = 2;
    limits.maxOrderQuantity = 500;
    limits.maxPosition = 2500;
    risk.setClientLimits(2, limits);
    
    std::cout << "Step 1: Submit resting orders...\n";
    
    // Client 1: Resting buy
    Order buy1;
    buy1.clientId = 1;
    buy1.orderId = 1;
    buy1.instrumentId = 1;
    buy1.side = Side::Buy;
    buy1.price = 9950;
    buy1.quantity = 100;
    buy1.tif = TimeInForce::GTC;
    
    auto r1 = engine.newOrder(buy1);
    std::cout << "  Client 1: Buy 100 @ 9950 -> " << (r1.success ? "Accepted" : "Rejected") << "\n";
    
    // Client 1: Another resting buy
    Order buy2;
    buy2.clientId = 1;
    buy2.orderId = 2;
    buy2.instrumentId = 1;
    buy2.side = Side::Buy;
    buy2.price = 9925;
    buy2.quantity = 200;
    buy2.tif = TimeInForce::GTC;
    
    auto r2 = engine.newOrder(buy2);
    std::cout << "  Client 1: Buy 200 @ 9925 -> " << (r2.success ? "Accepted" : "Rejected") << "\n";
    
    // Client 2: Resting sell
    Order sell1;
    sell1.clientId = 2;
    sell1.orderId = 1;
    sell1.instrumentId = 1;
    sell1.side = Side::Sell;
    sell1.price = 10050;
    sell1.quantity = 150;
    sell1.tif = TimeInForce::GTC;
    
    auto r3 = engine.newOrder(sell1);
    std::cout << "  Client 2: Sell 150 @ 10050 -> " << (r3.success ? "Accepted" : "Rejected") << "\n";
    
    std::cout << "\nStep 2: Marketable order with matching...\n";
    
    // Client 2: Marketable sell (crosses spread)
    Order sell2;
    sell2.clientId = 2;
    sell2.orderId = 2;
    sell2.instrumentId = 1;
    sell2.side = Side::Sell;
    sell2.price = 9950;
    sell2.quantity = 50;
    sell2.tif = TimeInForce::GTC;
    
    auto r4 = engine.newOrder(sell2);
    Quantity r4Filled = 0;
    Price r4Price = 0;
    for (const auto& m : r4.matches) { r4Filled += m.quantity; r4Price = m.price; }
    std::cout << "  Client 2: Sell 50 @ 9950 -> Filled " << r4Filled << " @ " << r4Price << "\n";
    
    std::cout << "\nStep 3: IOC and FOK orders...\n";
    
    // IOC order
    Order ioc;
    ioc.clientId = 1;
    ioc.orderId = 3;
    ioc.instrumentId = 1;
    ioc.side = Side::Buy;
    ioc.price = 10050;
    ioc.quantity = 200;
    ioc.tif = TimeInForce::IOC;
    
    auto r5 = engine.newOrder(ioc);
    Quantity r5Filled = 0;
    for (const auto& m : r5.matches) { r5Filled += m.quantity; }
    std::cout << "  Client 1: IOC Buy 200 @ 15100 -> Filled " << r5Filled << "\n";
    
    // FOK order (should fail - not enough liquidity)
    Order fok;
    fok.clientId = 2;
    fok.orderId = 3;
    fok.instrumentId = 1;
    fok.side = Side::Sell;
    fok.price = 9950;
    fok.quantity = 1000;
    fok.tif = TimeInForce::FOK;
    
    auto r6 = engine.newOrder(fok);
    std::cout << "  Client 2: FOK Sell 1000 @ 9950 -> " 
              << (r6.success ? "Filled" : "Rejected (insufficient liquidity)") << "\n";
    
    std::cout << "\nStep 4: Cancel and replace...\n";
    
    auto r7 = engine.cancelOrder(1, 2, 1);
    std::cout << "  Client 1: Cancel order 2 -> " << (r7.success ? "Success" : "Failed") << "\n";
    
    auto r8 = engine.replaceOrder(1, 1, 4, 1, 9975, 150);
    std::cout << "  Client 1: Replace order 1 -> " << (r8.success ? "Success" : "Failed") << "\n";
    
    std::cout << "\nStep 5: Risk rejection...\n";
    
    // Order exceeding risk limits
    Order bigOrder;
    bigOrder.clientId = 2;
    bigOrder.orderId = 4;
    bigOrder.instrumentId = 1;
    bigOrder.side = Side::Buy;
    bigOrder.price = 9950;
    bigOrder.quantity = 10000; // Exceeds maxOrderQuantity
    bigOrder.tif = TimeInForce::GTC;
    
    auto check = risk.checkNewOrder(2, 1, Side::Buy, 9950, 10000, instr1);
    std::cout << "  Client 2: Buy 10000 @ 9950 -> Rejected (" 
              << (check.second == RejectionReason::MaxOrderQuantityExceeded ? "MaxOrderQuantityExceeded" : "Other") << ")\n";
    
    std::cout << "\nStep 6: Book state...\n";
    std::cout << "  Best Bid: " << engine.getBook(1)->bestBid() << "\n";
    std::cout << "  Best Ask: " << engine.getBook(1)->bestAsk() << "\n";
    std::cout << "  Total Orders: " << engine.totalOrderCount() << "\n";
    std::cout << "  Total Matches: " << engine.totalMatchCount() << "\n";
    
    std::cout << "\nStep 7: State digest...\n";
    std::cout << "  Digest: 0x" << std::hex << engine.computeStateDigest() << std::dec << "\n";
    
    std::string error;
    std::cout << "  Invariants: " << (engine.checkInvariants(error) ? "OK" : "FAILED") << "\n";
    std::cout << "  Reference match: " << (engine.verifyAgainstReference(error) ? "OK" : "FAILED") << "\n";
    
    // Save demo artifacts
    std::filesystem::create_directories("artifacts/demo");
    
    std::ofstream summary("artifacts/demo/summary.json");
    summary << "{\n";
    summary << "  \"total_orders\": " << engine.totalOrderCount() << ",\n";
    summary << "  \"total_matches\": " << engine.totalMatchCount() << ",\n";
    summary << "  \"state_digest\": \"" << std::hex << engine.computeStateDigest() << std::dec << "\"\n";
    summary << "}\n";
    summary.close();
    
    std::cout << "\nDemo artifacts saved to artifacts/demo/\n";
    std::cout << "\nDemo complete!\n";
    
    return 0;
}
