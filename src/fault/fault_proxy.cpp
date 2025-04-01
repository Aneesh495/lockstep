#include "lockstep/fault/fault_proxy.hpp"

namespace lockstep {

FaultProxy::FaultProxy(std::uint64_t seed)
    : rng_(seed)
{}

void FaultProxy::setLossProbability(double prob) {
    lossProb_ = prob;
}

void FaultProxy::setDuplicateProbability(double prob) {
    dupProb_ = prob;
}

void FaultProxy::setReorderProbability(double prob) {
    reorderProb_ = prob;
}

void FaultProxy::setCorruptionProbability(double prob) {
    corruptProb_ = prob;
}

bool FaultProxy::processPacket(char channel, std::uint64_t seq, std::vector<std::uint8_t>& packet) {
    totalPackets_++;
    
    // Check for loss
    if (lossProb_ > 0 && rng_.nextBool(lossProb_)) {
        droppedPackets_++;
        return false; // Drop packet
    }
    
    // Check for corruption
    if (corruptProb_ > 0 && rng_.nextBool(corruptProb_)) {
        std::uint32_t bitPos = rng_.next(static_cast<std::uint32_t>(packet.size() * 8));
        std::uint32_t bytePos = bitPos / 8;
        std::uint32_t bitOffset = bitPos % 8;
        
        if (bytePos < packet.size()) {
            packet[bytePos] ^= (1 << bitOffset);
            corruptedPackets_++;
        }
    }
    
    // Check for reorder (swap with held packet)
    if (reorderProb_ > 0 && rng_.nextBool(reorderProb_) && !heldPacket_.empty()) {
        std::swap(packet, heldPacket_);
        std::swap(seq, heldSeq_);
        std::swap(channel, heldChannel_);
        reorderedPackets_++;
    }
    
    // Hold packet for potential reordering
    if (reorderProb_ > 0 && rng_.nextBool(reorderProb_)) {
        heldPacket_ = packet;
        heldSeq_ = seq;
        heldChannel_ = channel;
    }
    
    // Check for duplication
    if (dupProb_ > 0 && rng_.nextBool(dupProb_)) {
        duplicatedPackets_++;
        // Packet will be processed twice
    }
    
    return true;
}

void FaultProxy::resetStats() {
    totalPackets_ = 0;
    droppedPackets_ = 0;
    duplicatedPackets_ = 0;
    reorderedPackets_ = 0;
    corruptedPackets_ = 0;
}

} // namespace lockstep
