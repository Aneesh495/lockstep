#include "lockstep/network/udp_publisher.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include "lockstep/common/crc32c.hpp"
#include "lockstep/common/endian.hpp"

namespace lockstep {

UdpPublisher::UdpPublisher(std::uint16_t portA, std::uint16_t portB)
    : portA_(portA), portB_(portB), eventQueue_(10000) {}

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
    batch.reserve(20);

    MarketDataPacket packetA, packetB;

    while (running_ || !eventQueue_.empty()) {
        // Collect events
        batch.clear();
        MarketEvent event;

        while (batch.size() < 20 && eventQueue_.tryPop(event)) {
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

DeliveryEnvelope UdpPublisher::encodePacket(char channel, std::uint32_t session,
                                            std::uint64_t packetSeq,
                                            std::span<const MarketEvent> events) {
    if (events.empty() || events.size() > 20)
        throw std::invalid_argument("Invalid packet batch");
    DeliveryEnvelope env;
    env.channel = channel;
    env.sessionId = session;
    env.packetSeq = packetSeq;
    env.firstEventSeq = events.front().eventSeq;
    env.eventCount = static_cast<std::uint16_t>(events.size());
    env.payload.resize(FrameHeader::SIZE + 20, 0);
    ByteWriter md(env.payload.data() + FrameHeader::SIZE, 20);
    md.writeU64(env.firstEventSeq);
    md.writeU16(env.eventCount);
    md.writeU64(0);
    md.writeU16(0);
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        if (e.eventSeq != env.firstEventSeq + i || e.payload.size() > MAX_PAYLOAD_SIZE)
            throw std::invalid_argument("Noncontiguous event batch");
        env.payload.push_back(static_cast<std::uint8_t>(e.type));
        env.payload.push_back(static_cast<std::uint8_t>(e.payload.size() >> 8));
        env.payload.push_back(static_cast<std::uint8_t>(e.payload.size()));
        env.payload.insert(env.payload.end(), e.payload.begin(), e.payload.end());
    }
    if (env.payload.size() - FrameHeader::SIZE > MAX_PAYLOAD_SIZE)
        throw std::invalid_argument("Packet exceeds MTU");
    FrameHeader h;
    h.setMessageType(MessageType::SnapshotBegin);
    h.setSessionId(session);
    h.setSequence(packetSeq);
    h.setPayloadLength(static_cast<std::uint32_t>(env.payload.size() - FrameHeader::SIZE));
    h.serialize(env.payload.data(), env.payload.size());
    h.setCrc32c(Crc32C::compute(env.payload.data(), env.payload.size()));
    h.serialize(env.payload.data(), env.payload.size());
    return env;
}

bool UdpPublisher::sendPacket(int fd, std::uint16_t port, const MarketDataPacket& packet) {
    std::vector<MarketEvent> events;
    std::size_t offset = 0;
    for (std::uint16_t i = 0; i < packet.eventCount; ++i) {
        if (offset + 3 > packet.payload.size())
            return false;
        MarketEvent e;
        e.eventSeq = packet.firstEventSeq + i;
        e.type = static_cast<MessageType>(packet.payload[offset]);
        std::size_t length = readBeU16(packet.payload.data() + offset + 1);
        offset += 3;
        if (offset + length > packet.payload.size())
            return false;
        e.payload.assign(packet.payload.begin() + static_cast<std::ptrdiff_t>(offset),
                         packet.payload.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        events.push_back(std::move(e));
    }
    auto env = encodePacket('A', packet.sessionId, packet.packetSeq, events);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    ssize_t sent = sendto(fd, env.payload.data(), env.payload.size(), 0,
                          reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<ssize_t>(env.payload.size());
}
}  // namespace lockstep
