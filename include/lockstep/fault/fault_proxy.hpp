#pragma once

#include <cstdint>
#include <random>
#include "lockstep/common/types.hpp"

namespace lockstep {

// Deterministic fault injection for testing
// Seeded PRNG for reproducible fault schedules

enum class FaultType : std::uint8_t {
    None = 0,
    PacketLoss = 1,
    PacketDuplicate = 2,
    PacketReorder = 3,
    PacketDelay = 4,
    BitCorruption = 5,
    SessionReset = 6,
    ChannelOutage = 7,
    BothChannelGap = 8
};

struct FaultAction {
    FaultType type = FaultType::None;
    std::uint64_t targetSeq = 0;   // Sequence number affected
    std::uint32_t delayNs = 0;     // For delay faults
    std::uint32_t bitPosition = 0; // For corruption faults
    char channel = 'A';
};

// PCG32 PRNG for deterministic testing
class Pcg32 {
public:
    explicit Pcg32(std::uint64_t seed = 0)
        : state_(seed)
    {}
    
    std::uint32_t next() {
        std::uint64_t oldstate = state_;
        state_ = oldstate * 6364136223846793005ULL + inc_;
        std::uint32_t xorshifted = static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    
    std::uint32_t next(std::uint32_t max) {
        return next() % max;
    }
    
    bool nextBool(double probability) {
        return static_cast<double>(next()) / static_cast<double>(UINT32_MAX) < probability;
    }

private:
    std::uint64_t state_;
    std::uint64_t inc_ = 1;
};

class FaultProxy {
public:
    explicit FaultProxy(std::uint64_t seed = 0);
    
    // Configure fault profiles
    void setLossProbability(double prob);
    void setDuplicateProbability(double prob);
    void setReorderProbability(double prob);
    void setCorruptionProbability(double prob);
    
    // Process a packet - returns true if packet should be forwarded
    bool processPacket(char channel, std::uint64_t seq, std::vector<std::uint8_t>& packet);
    
    // Fault statistics
    std::uint64_t totalPacketsProcessed() const { return totalPackets_; }
    std::uint64_t packetsDropped() const { return droppedPackets_; }
    std::uint64_t packetsDuplicated() const { return duplicatedPackets_; }
    std::uint64_t packetsReordered() const { return reorderedPackets_; }
    std::uint64_t packetsCorrupted() const { return corruptedPackets_; }
    
    // Reset statistics
    void resetStats();

private:
    Pcg32 rng_;
    
    double lossProb_ = 0.0;
    double dupProb_ = 0.0;
    double reorderProb_ = 0.0;
    double corruptProb_ = 0.0;
    
    std::uint64_t totalPackets_ = 0;
    std::uint64_t droppedPackets_ = 0;
    std::uint64_t duplicatedPackets_ = 0;
    std::uint64_t reorderedPackets_ = 0;
    std::uint64_t corruptedPackets_ = 0;
    
    std::vector<std::uint8_t> heldPacket_;
    std::uint64_t heldSeq_ = 0;
    char heldChannel_ = 'A';
};

} // namespace lockstep
