#include "lockstep/fault/fault_proxy.hpp"
#include <utility>

namespace lockstep {

FaultProxy::FaultProxy(std::uint64_t seed) : rng_(seed) {}

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

void FaultProxy::setDelayProbability(double prob, std::uint32_t delayNs) {
    delayProb_ = prob;
    delayNs_ = delayNs;
}

void FaultProxy::setChannelOutage(char channel, bool outage) {
    if (channel == 'A' || channel == 'a') {
        outageA_ = outage;
    } else if (channel == 'B' || channel == 'b') {
        outageB_ = outage;
    }
}

std::vector<DeliveryEnvelope> FaultProxy::submit(DeliveryEnvelope envelope,
                                                 std::uint64_t currentNs) {
    totalPackets_++;

    // 1. Channel outage check
    if ((envelope.channel == 'A' && outageA_) || (envelope.channel == 'B' && outageB_)) {
        outagePackets_++;
        return {};
    }

    // 2. Packet loss check
    if (lossProb_ > 0.0 && rng_.nextBool(lossProb_)) {
        droppedPackets_++;
        return {};
    }

    // 3. Bit corruption check
    if (corruptProb_ > 0.0 && rng_.nextBool(corruptProb_)) {
        corruptedPackets_++;
        envelope.corrupted = true;
        if (!envelope.payload.empty()) {
            std::uint32_t bitPos =
                rng_.next(static_cast<std::uint32_t>(envelope.payload.size() * 8));
            std::size_t bytePos = bitPos / 8;
            std::uint8_t bitOffset = static_cast<std::uint8_t>(bitPos % 8);
            envelope.payload[bytePos] ^= static_cast<std::uint8_t>(1u << bitOffset);
        }
    }

    // 4. Delay check
    if (delayProb_ > 0.0 && rng_.nextBool(delayProb_)) {
        delayedPackets_++;
        envelope.scheduledDeliveryNs = currentNs + delayNs_;
    } else {
        envelope.scheduledDeliveryNs = currentNs;
    }

    // 5. Reordering check
    if (reorderProb_ > 0.0 && rng_.nextBool(reorderProb_) &&
        heldEnvelopes_.size() < MAX_REORDER_DEPTH) {
        reorderedPackets_++;
        heldEnvelopes_.push_back(std::move(envelope));
        return {};
    }

    std::vector<DeliveryEnvelope> out;
    out.push_back(std::move(envelope));

    // Release any previously held packet so it is delivered AFTER the current packet (actual
    // reorder!)
    if (!heldEnvelopes_.empty()) {
        out.push_back(std::move(heldEnvelopes_.front()));
        heldEnvelopes_.pop_front();
    }

    // 6. Duplication check
    if (dupProb_ > 0.0 && rng_.nextBool(dupProb_) && !out.empty()) {
        duplicatedPackets_++;
        out.push_back(out.front());
    }

    return out;
}

std::vector<DeliveryEnvelope> FaultProxy::drain() {
    std::vector<DeliveryEnvelope> out;
    while (!heldEnvelopes_.empty()) {
        out.push_back(std::move(heldEnvelopes_.front()));
        heldEnvelopes_.pop_front();
    }
    return out;
}

bool FaultProxy::processPacket(char channel, std::uint64_t seq, std::vector<std::uint8_t>& packet) {
    DeliveryEnvelope env;
    env.channel = channel;
    env.packetSeq = seq;
    env.payload = std::move(packet);

    auto result = submit(std::move(env));
    if (result.empty()) {
        packet.clear();
        return false;
    }

    packet = std::move(result[0].payload);
    return true;
}

void FaultProxy::resetStats() {
    totalPackets_ = 0;
    droppedPackets_ = 0;
    duplicatedPackets_ = 0;
    reorderedPackets_ = 0;
    corruptedPackets_ = 0;
    delayedPackets_ = 0;
    outagePackets_ = 0;
    heldEnvelopes_.clear();
}

}  // namespace lockstep
