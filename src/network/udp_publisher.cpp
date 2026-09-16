#include "lockstep/network/udp_publisher.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace lockstep {

UdpPublisher::UdpPublisher(std::uint16_t portA, std::uint16_t portB)
    : portA_(portA)
    , portB_(portB)
    , eventQueue_(10000)
{
}

UdpPublisher::~UdpPublisher() {
    stop();
}

bool UdpPublisher::start() {
    socketA_ = socket(AF_INET, SOCK_DGRAM, 0);
    socketB_ = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (socketA_ < 0 || socketB_ < 0) {
        return false;
    }
    
    running_ = true;
    thread_ = std::thread(&UdpPublisher::run, this);
    
    return true;
}

void UdpPublisher::stop() {
    running_ = false;
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    if (socketA_ >= 0) {
        ::close(socketA_);
        socketA_ = -1;
    }
    
    if (socketB_ >= 0) {
        ::close(socketB_);
        socketB_ = -1;
    }
}

void UdpPublisher::run() {
    std::vector<MarketEvent> batch;
    batch.reserve(100);
    
    MarketDataPacket packetA, packetB;
    
    while (running_) {
        // Collect events
        batch.clear();
        MarketEvent event;
        
        while (eventQueue_.tryPop(event) && batch.size() < 100) {
            batch.push_back(event);
        }
        
        if (batch.empty()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Build packet
        packetA.sessionId = sessionId_;
        packetA.packetSeq = packetSeqA_++;
        packetA.firstEventSeq = batch.front().eventSeq;
        packetA.eventCount = static_cast<std::uint16_t>(batch.size());
        packetA.payload.clear();
        
        for (const auto& e : batch) {
            // Serialize event
            std::uint8_t typeByte = static_cast<std::uint8_t>(e.type);
            packetA.payload.push_back(typeByte);
            
            std::uint8_t lenBytes[2];
            lenBytes[0] = static_cast<std::uint8_t>(e.payload.size() >> 8);
            lenBytes[1] = static_cast<std::uint8_t>(e.payload.size() & 0xFF);
            packetA.payload.insert(packetA.payload.end(), lenBytes, lenBytes + 2);
            
            packetA.payload.insert(packetA.payload.end(), e.payload.begin(), e.payload.end());
        }
        
        // Send same packet to both channels
        packetB = packetA;
        packetB.packetSeq = packetSeqB_++;
        
        sendPacket(socketA_, portA_, packetA);
        sendPacket(socketB_, portB_, packetB);
    }
}

bool UdpPublisher::sendPacket(int fd, std::uint16_t port, const MarketDataPacket& packet) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    
    // Build frame
    std::vector<std::uint8_t> buffer;
    buffer.reserve(FRAME_HEADER_SIZE + packet.payload.size() + 20);
    
    // Frame header
    FrameHeader header;
    header.setMessageType(MessageType::SnapshotBegin);
    header.setSessionId(packet.sessionId);
    header.setSequence(packet.packetSeq);
    header.setSendTimestampNs(0);
    header.setPayloadLength(static_cast<std::uint32_t>(packet.payload.size() + 20));
    
    std::vector<std::uint8_t> headerBytes(FRAME_HEADER_SIZE);
    header.serialize(headerBytes.data(), headerBytes.size());
    buffer.insert(buffer.end(), headerBytes.begin(), headerBytes.end());
    
    // Market data header
    std::uint8_t mdHeader[20];
    std::memcpy(mdHeader, &packet.firstEventSeq, 8);
    std::memcpy(mdHeader + 8, &packet.eventCount, 2);
    buffer.insert(buffer.end(), mdHeader, mdHeader + 20);
    
    // Payload
    buffer.insert(buffer.end(), packet.payload.begin(), packet.payload.end());
    
    ssize_t sent = sendto(fd, buffer.data(), buffer.size(), 0,
                          reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    
    return sent > 0;
}

} // namespace lockstep
