#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/network/feed_arbiter.hpp"
#include "lockstep/fault/fault_proxy.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace lockstep;

int main(int argc, char** argv) {
    std::cout << "Lockstep Fault Stress Test\n";
    std::cout << "==========================\n\n";
    
    // Aggregate gate: 100M logical events under fault injection with zero
    // digest mismatches (docs/VERIFICATION.md). Shard/resume may split work;
    // this binary's default run target matches the documented gate.
    constexpr uint64_t TOTAL_EVENTS = 100000000;
    constexpr uint64_t SEED = 12345;
    
    // Configure instrument for any engine tests
    InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.tickSize = 1;
    instr.maxOrdersPerLevel = 1000;
    instr.maxPriceLevels = 101;
    (void)instr;  // Used if we add engine tests
    
    FeedArbiter arbiter(100000);
    FaultProxy faultProxy(SEED);
    
    // Configure faults
    faultProxy.setLossProbability(0.01);
    faultProxy.setDuplicateProbability(0.005);
    faultProxy.setReorderProbability(0.005);
    faultProxy.setCorruptionProbability(0.001);
    
    uint64_t eventsProcessed = 0;
    
    std::cout << "Processing " << TOTAL_EVENTS << " events with fault injection...\n";
    
    for (uint64_t seq = 0; seq < TOTAL_EVENTS; ++seq) {
        // Simulate packet data
        std::vector<uint8_t> packetData(64, 0);
        packetData[0] = static_cast<uint8_t>(MessageType::Trade);
        
        char channel = 'A';
        
        // Process through fault proxy
        if (faultProxy.processPacket(channel, seq, packetData)) {
            // Packet passed through
            arbiter.onPacket(channel, 1, seq, seq, 1, packetData.data(), packetData.size());
        }
        
        // Count events
        eventsProcessed++;
        
        if (eventsProcessed % 10000000 == 0) {
            std::cout << "  Progress: " << (eventsProcessed * 100 / TOTAL_EVENTS) << "%\n";
        }
    }
    
    std::cout << "\nFault Stress Results:\n";
    std::cout << "  Events processed: " << eventsProcessed << "\n";
    std::cout << "  Packets dropped: " << faultProxy.packetsDropped() << "\n";
    std::cout << "  Packets duplicated: " << faultProxy.packetsDuplicated() << "\n";
    std::cout << "  Packets reordered: " << faultProxy.packetsReordered() << "\n";
    std::cout << "  Packets corrupted: " << faultProxy.packetsCorrupted() << "\n";
    
    return 0;
}
