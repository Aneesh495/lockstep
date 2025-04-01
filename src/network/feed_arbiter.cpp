#include "lockstep/network/feed_arbiter.hpp"
#include <algorithm>

namespace lockstep {

FeedArbiter::FeedArbiter(std::uint32_t maxBufferSize)
    : maxBufferSize_(maxBufferSize)
{}

void FeedArbiter::onPacket(char channel, std::uint32_t sessionId, std::uint64_t packetSeq,
                           std::uint64_t firstEventSeq, std::uint16_t eventCount,
                           const std::uint8_t* data, std::size_t length) {
    // Validate session
    if (sessionId_ == 0) {
        sessionId_ = sessionId;
    } else if (sessionId != sessionId_) {
        // Session changed - need to resnapshot
        state_ = State::Stale;
        return;
    }
    
    // Update channel state
    auto& state = (channel == 'A') ? channelA_ : channelB_;
    state.lastPacketSeq = packetSeq;
    state.receivedPackets++;
    
    // Buffer events
    std::uint64_t eventSeq = firstEventSeq;
    std::size_t offset = 0;
    
    for (std::uint16_t i = 0; i < eventCount; ++i) {
        if (offset + 3 > length) break;
        
        std::uint8_t type = data[offset++];
        std::uint16_t eventLen = static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) | data[offset + 1]);
        offset += 2;
        
        if (offset + eventLen > length) break;
        
        BufferedEvent event;
        event.eventSeq = eventSeq++;
        event.type = static_cast<MessageType>(type);
        event.payload.assign(data + offset, data + offset + eventLen);
        event.channel = channel;
        
        bufferEvent(event);
        offset += eventLen;
    }
    
    // Try to emit events in order
    tryEmitEvents();
}

void FeedArbiter::bufferEvent(const BufferedEvent& event) {
    // Check for gap
    if (nextExpectedSeq_ == 0) {
        nextExpectedSeq_ = event.eventSeq;
    }
    
    if (event.eventSeq < nextExpectedSeq_) {
        // Duplicate
        return;
    }
    
    if (event.eventSeq >= nextExpectedSeq_ + maxBufferSize_) {
        // Buffer overflow
        state_ = State::Gap;
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
                    // Gap detected
                    gapStart_ = nextExpectedSeq_;
                    gapEnd_ = minSeq;
                    state_ = State::Gap;
                }
            }
            break;
        }
        
        // Emit event
        readyEvents_.push_back(it->second);
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
    
    BufferedEvent event = readyEvents_.front();
    readyEvents_.pop_front();
    
    processedEvents_++;
    
    return event;
}

void FeedArbiter::applySnapshot(std::uint64_t snapshotSeq) {
    // Clear buffer up to snapshot
    std::erase_if(eventBuffer_, [snapshotSeq](const auto& pair) {
        return pair.first <= snapshotSeq;
    });
    
    nextExpectedSeq_ = snapshotSeq + 1;
    state_ = State::Recovering;
    
    // Try to emit buffered events
    tryEmitEvents();
    
    if (state_ != State::Gap) {
        state_ = State::Healthy;
    }
}

} // namespace lockstep
