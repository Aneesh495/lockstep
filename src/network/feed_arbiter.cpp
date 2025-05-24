#include "lockstep/network/feed_arbiter.hpp"
#include <algorithm>
#include "lockstep/common/crc32c.hpp"

namespace lockstep {

FeedArbiter::FeedArbiter(std::uint32_t maxBufferSize) : maxBufferSize_(maxBufferSize) {}

void FeedArbiter::onEnvelope(const DeliveryEnvelope& env) {
    if (env.corrupted) {
        corruptDiscarded_++;
        return;
    }

    onPacket(env.channel, env.sessionId, env.packetSeq, env.firstEventSeq, env.eventCount,
             env.payload.data(), env.payload.size());
}

void FeedArbiter::onPacket(char channel, std::uint32_t sessionId, std::uint64_t packetSeq,
                           std::uint64_t firstEventSeq, std::uint16_t eventCount,
                           const std::uint8_t* data, std::size_t length) {
    // Validate session
    if (sessionId_ == 0) {
        sessionId_ = sessionId;
    } else if (sessionId != sessionId_) {
        // Session changed - need to resnapshot
        state_ = State::Stale;
        sessionChanges_++;
        return;
    }

    if (state_ == State::Stale) {
        return;
    }

    // Update channel state
    auto& state = (channel == 'A') ? channelA_ : channelB_;
    state.lastPacketSeq = packetSeq;
    state.receivedPackets++;

    // Check if packet is encapsulated with a FrameHeader
    const std::uint8_t* eventData = data;
    std::size_t eventLenTotal = length;

    if (length >= FrameHeader::SIZE && data[0] == 'L' && data[1] == 'K' && data[2] == 'S' &&
        data[3] == 'T') {
        auto hdr = FrameHeader::parse(data, length);
        if (!hdr.has_value()) {
            corruptDiscarded_++;
            return;
        }

        // Check CRC if set
        if (hdr->crc32c() != 0) {
            std::vector<std::uint8_t> temp(data, data + hdr->totalSize());
            temp[32] = 0;
            temp[33] = 0;
            temp[34] = 0;
            temp[35] = 0;
            std::uint32_t computed = Crc32C::compute(temp.data(), temp.size());
            if (computed != hdr->crc32c()) {
                corruptDiscarded_++;
                return;
            }
        }

        eventData = data + FrameHeader::SIZE;
        eventLenTotal = length - FrameHeader::SIZE;
    }

    // Buffer events
    std::uint64_t eventSeq = firstEventSeq;
    std::size_t offset = 0;

    for (std::uint16_t i = 0; i < eventCount; ++i) {
        if (offset + 3 > eventLenTotal) {
            corruptDiscarded_++;
            return;
        }

        std::uint8_t type = eventData[offset++];
        std::uint16_t eventLen = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(eventData[offset]) << 8) | eventData[offset + 1]);
        offset += 2;

        if (offset + eventLen > eventLenTotal) {
            corruptDiscarded_++;
            return;
        }

        BufferedEvent event;
        event.eventSeq = eventSeq++;
        event.type = static_cast<MessageType>(type);
        event.payload.assign(eventData + offset, eventData + offset + eventLen);
        event.channel = channel;

        bufferEvent(event);
        offset += eventLen;
    }

    // Try to emit events in order
    tryEmitEvents();
}

void FeedArbiter::bufferEvent(const BufferedEvent& event) {
    if (nextExpectedSeq_ == 0) {
        nextExpectedSeq_ = event.eventSeq;
    }

    if (event.eventSeq < nextExpectedSeq_) {
        // Duplicate: already emitted
        duplicatesDiscarded_++;
        return;
    }

    if (eventBuffer_.find(event.eventSeq) != eventBuffer_.end()) {
        // Duplicate: already in buffer from other channel
        duplicatesDiscarded_++;
        return;
    }

    if (event.eventSeq >= nextExpectedSeq_ + maxBufferSize_) {
        // Buffer overflow
        state_ = State::Gap;
        bufferOverflows_++;
        return;
    }

    eventBuffer_[event.eventSeq] = event;
}

void FeedArbiter::tryEmitEvents() {
    while (true) {
        auto it = eventBuffer_.find(nextExpectedSeq_);
        if (it == eventBuffer_.end()) {
            // Check for gap
            if (!eventBuffer_.empty()) {
                std::uint64_t minSeq = eventBuffer_.begin()->first;
                if (minSeq > nextExpectedSeq_) {
                    if (state_ != State::Gap) {
                        gapsDetected_++;
                    }
                    gapStart_ = nextExpectedSeq_;
                    gapEnd_ = minSeq;
                    state_ = State::Gap;
                }
            }
            break;
        }

        // Emit event
        readyEvents_.push_back(std::move(it->second));
        eventBuffer_.erase(it);
        nextExpectedSeq_++;

        // Update state
        if (state_ == State::Gap && gapEnd_ > 0 && nextExpectedSeq_ >= gapEnd_) {
            state_ = State::Healthy;
            gapStart_ = 0;
            gapEnd_ = 0;
        }
    }
}

bool FeedArbiter::hasReadyEvents() const {
    return !readyEvents_.empty();
}

std::optional<BufferedEvent> FeedArbiter::nextEvent() {
    if (readyEvents_.empty()) {
        return std::nullopt;
    }

    BufferedEvent event = std::move(readyEvents_.front());
    readyEvents_.pop_front();

    processedEvents_++;

    return event;
}

void FeedArbiter::applySnapshot(std::uint64_t snapshotSeq, std::uint32_t newSessionId) {
    if (newSessionId != 0) {
        sessionId_ = newSessionId;
    }

    // Discard ready events that are obsolete (<= snapshotSeq)
    std::erase_if(readyEvents_,
                  [snapshotSeq](const BufferedEvent& ev) { return ev.eventSeq <= snapshotSeq; });

    // Discard buffered events that are obsolete (<= snapshotSeq)
    std::erase_if(eventBuffer_,
                  [snapshotSeq](const auto& pair) { return pair.first <= snapshotSeq; });

    nextExpectedSeq_ = snapshotSeq + 1;
    gapStart_ = 0;
    gapEnd_ = 0;
    snapshotsInstalled_++;
    state_ = State::Recovering;

    // Try to emit buffered events starting from snapshotSeq + 1
    tryEmitEvents();

    if (state_ != State::Gap) {
        state_ = State::Healthy;
    }
}

void FeedArbiter::resetStats() {
    processedEvents_ = 0;
    duplicatesDiscarded_ = 0;
    gapsDetected_ = 0;
    snapshotsInstalled_ = 0;
    corruptDiscarded_ = 0;
    bufferOverflows_ = 0;
    sessionChanges_ = 0;
    gapStart_ = 0;
    gapEnd_ = 0;
    state_ = State::Healthy;
    eventBuffer_.clear();
    readyEvents_.clear();
    channelA_ = ChannelState{};
    channelB_ = ChannelState{};
}

}  // namespace lockstep
