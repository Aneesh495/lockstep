#pragma once

#include <cstdint>
#include <map>
#include <deque>
#include <optional>
#include "lockstep/common/types.hpp"
#include "lockstep/protocol/frame.hpp"

namespace lockstep {

// Merges redundant UDP feeds A and B
// Deduplicates and emits events in sequence order
// Detects gaps and triggers snapshot recovery

struct BufferedEvent {
    std::uint64_t eventSeq = 0;
    MessageType type = MessageType::Trade;
    std::vector<std::uint8_t> payload;
    char channel = 'A';
};

struct ChannelState {
    std::uint64_t lastPacketSeq = 0;
    std::uint64_t receivedPackets = 0;
};

class FeedArbiter {
public:
    enum class State {
        Healthy,
        Gap,
        Recovering,
        Stale
    };
    
    explicit FeedArbiter(std::uint32_t maxBufferSize = 100000);
    
    // Process a packet from feed A or B
    void onPacket(char channel, std::uint32_t sessionId, std::uint64_t packetSeq,
                  std::uint64_t firstEventSeq, std::uint16_t eventCount,
                  const std::uint8_t* data, std::size_t length);
    
    // Get next event in order
    std::optional<BufferedEvent> nextEvent();
    bool hasReadyEvents() const;
    
    // Apply snapshot and replay buffered events
    void applySnapshot(std::uint64_t snapshotSeq);
    
    // State queries
    State state() const { return state_; }
    std::uint64_t nextExpectedSeq() const { return nextExpectedSeq_; }
    std::uint64_t processedEvents() const { return processedEvents_; }
    
    // Gap info
    std::uint64_t gapStart() const { return gapStart_; }
    std::uint64_t gapEnd() const { return gapEnd_; }
    bool hasGap() const { return state_ == State::Gap; }

private:
    void bufferEvent(const BufferedEvent& event);
    void tryEmitEvents();
    
    std::uint32_t maxBufferSize_;
    std::uint32_t sessionId_ = 0;
    
    State state_ = State::Healthy;
    std::uint64_t nextExpectedSeq_ = 0;
    std::uint64_t processedEvents_ = 0;
    
    std::map<std::uint64_t, BufferedEvent> eventBuffer_;
    std::deque<BufferedEvent> readyEvents_;
    
    ChannelState channelA_;
    ChannelState channelB_;
    
    std::uint64_t gapStart_ = 0;
    std::uint64_t gapEnd_ = 0;
};

} // namespace lockstep
