#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include "lockstep/common/types.hpp"
#include "lockstep/common/virtual_clock.hpp"
#include "lockstep/engine/order_book.hpp"
#include "lockstep/engine/reference_book.hpp"

namespace lockstep {

// Matching engine for multiple instruments.
// Single-threaded deterministic execution; fixed-capacity / zero-allocation
// hot path after init. Maintains optimized + reference books for differential
// testing. Isolated core gates: >=5M commands/s and p99 <1us (docs/BENCHMARKS.md).

class MatchingEngine {
public:
    struct Config {
        std::vector<InstrumentConfig> instruments;
        bool useReferenceBook = true; // For differential testing
    };
    
    struct Result {
        bool success = false;
        RejectionReason reason = RejectionReason::None;
        std::vector<Match> matches;
        std::uint64_t commandSeq = 0;
        std::uint64_t eventSeq = 0;
    };
    
    explicit MatchingEngine(const Config& config);
    
    // Order operations
    Result newOrder(const Order& order);
    Result cancelOrder(ClientId clientId, OrderId orderId, InstrumentId instrumentId = INVALID_INSTRUMENT_ID);
    Result replaceOrder(ClientId clientId, OrderId oldOrderId, OrderId newOrderId,
                       InstrumentId instrumentId, Price newPrice, Quantity newQuantity);
    std::uint32_t massCancel(ClientId clientId);
    
    // Kill switch
    void setKillSwitch(bool active);
    bool isKillSwitchActive() const { return killSwitchActive_; }
    
    // Sequencing
    std::uint64_t nextCommandSeq() { return commandSeq_++; }
    std::uint64_t nextEventSeq() { return eventSeq_++; }
    std::uint64_t currentCommandSeq() const { return commandSeq_; }
    std::uint64_t currentEventSeq() const { return eventSeq_; }
    
    // Access
    OrderBook* getBook(InstrumentId instrumentId);
    const OrderBook* getBook(InstrumentId instrumentId) const;
    
    // Clock access
    VirtualClock& clock() { return clock_; }
    const VirtualClock& clock() const { return clock_; }
    
    // Statistics
    std::uint32_t totalOrderCount() const;
    std::uint64_t totalMatchCount() const { return totalMatches_; }
    
    // State digest
    std::uint64_t computeStateDigest() const;
    
    // Differential verification
    bool verifyAgainstReference(std::string& error) const;
    
    // Invariant checking
    bool checkInvariants(std::string& error) const;

private:
    Config config_;
    VirtualClock clock_;
    
    // Optimized books
    std::unordered_map<InstrumentId, std::unique_ptr<OrderBook>> books_;
    
    // Reference books for differential testing
    std::unordered_map<InstrumentId, std::unique_ptr<ReferenceBook>> refBooks_;
    
    // Sequences
    std::uint64_t commandSeq_ = 1;
    std::uint64_t eventSeq_ = 1;
    
    // Kill switch
    bool killSwitchActive_ = false;
    
    // Statistics
    std::uint64_t totalMatches_ = 0;
    
    // Instrument config lookup
    std::unordered_map<InstrumentId, InstrumentConfig> instrumentConfigs_;
};

} // namespace lockstep
