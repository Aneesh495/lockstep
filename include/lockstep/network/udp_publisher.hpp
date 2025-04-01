#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include "lockstep/common/types.hpp"
#include "lockstep/concurrency/spsc_ring.hpp"
#include "lockstep/protocol/frame.hpp"

namespace lockstep {

// UDP market data publisher
// Publishes the same event stream over two independent channels (A and B)

struct MarketEvent {
    std::uint64_t eventSeq = 0;
    MessageType type = MessageType::Trade;
    std::vector<std::uint8_t> payload;
    Timestamp timestamp = 0;
};

struct MarketDataPacket {
    std::uint32_t sessionId = 0;
    std::uint64_t packetSeq = 0;
    std::uint64_t firstEventSeq = 0;
    std::uint16_t eventCount = 0;
    std::vector<std::uint8_t> payload;
};

class UdpPublisher {
public:
    UdpPublisher(std::uint16_t portA, std::uint16_t portB);
    ~UdpPublisher();
    
    bool start();
    void stop();
    
    // Event queue (consumer)
    SpscRing<MarketEvent>& eventQueue() { return eventQueue_; }
    
    void setSessionId(std::uint32_t sessionId) { sessionId_ = sessionId; }
    std::uint32_t sessionId() const { return sessionId_; }

private:
    void run();
    bool sendPacket(int fd, std::uint16_t port, const MarketDataPacket& packet);
    
    std::uint16_t portA_;
    std::uint16_t portB_;
    int socketA_ = -1;
    int socketB_ = -1;
    
    std::uint32_t sessionId_ = 0;
    std::uint64_t packetSeqA_ = 0;
    std::uint64_t packetSeqB_ = 0;
    
    std::atomic<bool> running_{false};
    std::thread thread_;
    
    SpscRing<MarketEvent> eventQueue_;
};

} // namespace lockstep
