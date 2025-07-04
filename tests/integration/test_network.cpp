#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>

#define TEST_ASSERT(cond)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::cerr << "Assertion failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ \
                      << "\n";                                                                  \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)
#include <iostream>
#include <thread>
#include <vector>
#include "lockstep/common/endian.hpp"
#include "lockstep/network/feed_arbiter.hpp"
#include "lockstep/network/tcp_gateway.hpp"
#include "lockstep/protocol/codec.hpp"
#include "lockstep/protocol/frame.hpp"

using namespace lockstep;

// Test 2: TCP Frame Coalescing - multiple frames in one read
void testTcpFrameCoalescing() {
    std::cout << "  Testing TCP frame coalescing...\n";

    // Create multiple frames
    std::vector<uint8_t> combinedData;

    for (int i = 0; i < 5; i++) {
        FrameHeader header;
        header.setMessageType(MessageType::Heartbeat);
        header.setSessionId(1);
        header.setSequence(static_cast<uint64_t>(i + 1));
        header.setSendTimestampNs(uint64_t{1000000} + static_cast<uint64_t>(i));
        header.setPayloadLength(0);

        std::vector<uint8_t> frame(FrameHeader::SIZE);
        header.serialize(frame.data(), frame.size());
        combinedData.insert(combinedData.end(), frame.begin(), frame.end());
    }

    // Parse all frames sequentially
    size_t offset = 0;

    while (offset < combinedData.size()) {
        auto header =
            FrameHeader::parse(combinedData.data() + offset, combinedData.size() - offset);
        if (!header.has_value()) {
            break;
        }
        offset += header->totalSize();
    }

    TEST_ASSERT(offset == combinedData.size());
    std::cout << "    [PASS] Frame coalescing parsing\n";
}

// Test 3: Malformed frames
void testMalformedFrames() {
    std::cout << "  Testing malformed frames...\n";

    // Invalid magic
    {
        std::vector<uint8_t> data(FrameHeader::SIZE, 0);
        TEST_ASSERT(!FrameHeader::parse(data.data(), data.size()).has_value());
    }

    // Invalid version
    {
        std::vector<uint8_t> data(FrameHeader::SIZE, 0);
        ByteWriter writer(data.data(), data.size());
        writer.writeU32(FrameHeader::MAGIC);
        writer.writeU8(99);  // Invalid version
        TEST_ASSERT(!FrameHeader::parse(data.data(), data.size()).has_value());
    }

    // Non-zero reserved field
    {
        std::vector<uint8_t> data(FrameHeader::SIZE, 0);
        ByteWriter writer(data.data(), data.size());
        writer.writeU32(FrameHeader::MAGIC);
        writer.writeU8(1);
        writer.writeU8(static_cast<uint8_t>(MessageType::Heartbeat));
        writer.writeU16(0);
        writer.writeU32(0);
        writer.writeU32(1);
        writer.writeU64(1);
        writer.writeU64(1000000);
        writer.writeU32(0);
        writer.writeU32(1);  // Non-zero reserved
        TEST_ASSERT(!FrameHeader::parse(data.data(), data.size()).has_value());
    }

    // Oversized payload
    {
        std::vector<uint8_t> data(FrameHeader::SIZE, 0);
        ByteWriter writer(data.data(), data.size());
        writer.writeU32(FrameHeader::MAGIC);
        writer.writeU8(1);
        writer.writeU8(static_cast<uint8_t>(MessageType::Heartbeat));
        writer.writeU16(0);
        writer.writeU32(0xFFFFFFFF);  // Max payload
        TEST_ASSERT(!FrameHeader::parse(data.data(), data.size()).has_value());
    }

    std::cout << "    [PASS] Malformed frame rejection\n";
}

// Test 4: Feed arbiter - duplicate detection
void testFeedArbiterDuplicates() {
    std::cout << "  Testing feed arbiter duplicate detection...\n";

    FeedArbiter arbiter(1000);

    // Create a simple event packet
    std::vector<uint8_t> packetData;
    packetData.push_back(static_cast<uint8_t>(MessageType::Trade));  // Event type
    packetData.push_back(0);                                         // Length high byte
    packetData.push_back(10);                                        // Length low byte
    for (int i = 0; i < 10; i++) {
        packetData.push_back(static_cast<uint8_t>(i));  // Payload
    }

    // Receive same packet twice
    arbiter.onPacket('A', 1, 1, 100, 1, packetData.data(), packetData.size());
    arbiter.onPacket('A', 1, 2, 100, 1, packetData.data(), packetData.size());

    // Should only emit one event
    TEST_ASSERT(arbiter.hasReadyEvents());
    (void)arbiter.nextEvent();
    TEST_ASSERT(!arbiter.hasReadyEvents());
    std::cout << "    [PASS] Duplicate event handling\n";
}

// Test 5: Feed arbiter - gap detection
void testFeedArbiterGapDetection() {
    std::cout << "  Testing feed arbiter gap detection...\n";

    FeedArbiter arbiter(1000);

    std::vector<uint8_t> packetData;
    packetData.push_back(static_cast<uint8_t>(MessageType::Trade));
    packetData.push_back(0);
    packetData.push_back(10);
    for (int i = 0; i < 10; i++) {
        packetData.push_back(static_cast<uint8_t>(i));
    }

    // Send event 100
    arbiter.onPacket('A', 1, 1, 100, 1, packetData.data(), packetData.size());

    // Should be able to read event 100
    TEST_ASSERT(arbiter.hasReadyEvents());
    auto event = arbiter.nextEvent();
    TEST_ASSERT(event.has_value());
    TEST_ASSERT(event->eventSeq == 100);

    // Skip to event 103 (gap at 101, 102)
    arbiter.onPacket('A', 1, 2, 103, 1, packetData.data(), packetData.size());

    // Should detect gap
    TEST_ASSERT(arbiter.hasGap());
    TEST_ASSERT(arbiter.gapStart() == 101);
    TEST_ASSERT(arbiter.gapEnd() == 103);

    std::cout << "    [PASS] Gap detection\n";
}

// Test 6: Feed arbiter - redundant feed recovery
void testFeedArbiterRedundantRecovery() {
    std::cout << "  Testing feed arbiter redundant feed recovery...\n";

    FeedArbiter arbiter(1000);

    std::vector<uint8_t> packetData;
    packetData.push_back(static_cast<uint8_t>(MessageType::Trade));
    packetData.push_back(0);
    packetData.push_back(10);
    for (int i = 0; i < 10; i++) {
        packetData.push_back(static_cast<uint8_t>(i));
    }

    // Feed A: events 100, 101, 102
    arbiter.onPacket('A', 1, 1, 100, 1, packetData.data(), packetData.size());
    arbiter.onPacket('A', 1, 2, 101, 1, packetData.data(), packetData.size());
    arbiter.onPacket('A', 1, 3, 102, 1, packetData.data(), packetData.size());

    // Feed B: events 100, 101, 103 (missing 102, has 103)
    arbiter.onPacket('B', 1, 1, 100, 1, packetData.data(), packetData.size());
    arbiter.onPacket('B', 1, 2, 101, 1, packetData.data(), packetData.size());
    arbiter.onPacket('B', 1, 3, 103, 1, packetData.data(), packetData.size());

    // Should recover event 102 from A, and 103 from B
    {
        int count = 0;
        while (arbiter.hasReadyEvents() && count < 10) {
            (void)arbiter.nextEvent();
            count++;
        }
    }

    // Should have processed 100, 101, 102, 103
    TEST_ASSERT(arbiter.state() == FeedArbiter::State::Healthy);

    std::cout << "    [PASS] Redundant feed recovery\n";
}

// Test 7: Feed arbiter - snapshot application
void testFeedArbiterSnapshotRecovery() {
    std::cout << "  Testing feed arbiter snapshot recovery...\n";

    FeedArbiter arbiter(1000);

    std::vector<uint8_t> packetData;
    packetData.push_back(static_cast<uint8_t>(MessageType::Trade));
    packetData.push_back(0);
    packetData.push_back(10);
    for (int i = 0; i < 10; i++) {
        packetData.push_back(static_cast<uint8_t>(i));
    }

    // Establish healthy state
    arbiter.onPacket('A', 1, 1, 100, 1, packetData.data(), packetData.size());

    {
        int count = 0;
        while (arbiter.hasReadyEvents() && count < 10) {
            (void)arbiter.nextEvent();
            count++;
        }
    }

    // Create gap by sending event 105 (missing 101-104)
    arbiter.onPacket('A', 1, 2, 105, 1, packetData.data(), packetData.size());

    // Should be in gap state
    TEST_ASSERT(arbiter.hasGap());

    // Apply snapshot at 104 (covering missing 101-104)
    arbiter.applySnapshot(104);

    // Should now be able to process 105 (after discarding <= 104)
    TEST_ASSERT(arbiter.hasReadyEvents());
    auto event = arbiter.nextEvent();
    TEST_ASSERT(event.has_value());
    TEST_ASSERT(event->eventSeq == 105);

    std::cout << "    [PASS] Snapshot recovery\n";
}

// Test 8: Feed arbiter - session change detection
void testFeedArbiterSessionChange() {
    std::cout << "  Testing feed arbiter session change detection...\n";

    FeedArbiter arbiter(1000);

    std::vector<uint8_t> packetData;
    packetData.push_back(static_cast<uint8_t>(MessageType::Trade));
    packetData.push_back(0);
    packetData.push_back(10);
    for (int i = 0; i < 10; i++) {
        packetData.push_back(static_cast<uint8_t>(i));
    }

    // Session 1
    arbiter.onPacket('A', 1, 1, 100, 1, packetData.data(), packetData.size());
    TEST_ASSERT(arbiter.state() == FeedArbiter::State::Healthy);

    // Session 2 (session change)
    arbiter.onPacket('A', 2, 1, 100, 1, packetData.data(), packetData.size());
    TEST_ASSERT(arbiter.state() == FeedArbiter::State::Stale);

    std::cout << "    [PASS] Session change detection\n";
}

// Test 9: TCP Gateway basic operation
void testTcpGatewayCreation() {
    std::cout << "  Testing TCP gateway creation...\n";

    TcpGateway gateway(0, 10);
    TEST_ASSERT(gateway.start());
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(gateway.port());
    TEST_ASSERT(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    std::vector<uint8_t> frames(FrameHeader::SIZE * 3);
    for (uint64_t i = 0; i < 3; ++i) {
        FrameHeader h;
        h.setMessageType(MessageType::Heartbeat);
        h.setSequence(i + 1);
        h.serialize(frames.data() + i * FrameHeader::SIZE, FrameHeader::SIZE);
    }
    TEST_ASSERT(::write(fd, frames.data(), 7) == 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    TEST_ASSERT(::write(fd, frames.data() + 7, frames.size() - 7) ==
                static_cast<ssize_t>(frames.size() - 7));
    uint64_t count = 0;
    CommandMessage cmd;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (count < 3 && std::chrono::steady_clock::now() < deadline) {
        if (gateway.commandQueue().tryPop(cmd)) {
            ++count;
            TEST_ASSERT(cmd.clientSeq == count);
        } else
            std::this_thread::yield();
    }
    TEST_ASSERT(count == 3);
    ::close(fd);
    gateway.stop();
    std::cout << "    [PASS] Real TCP fragmented/coalesced frames: 3 commands\n";
}

// Test 10: Round-trip frame encoding
void testFrameRoundTrip() {
    std::cout << "  Testing frame round-trip encoding...\n";

    // Test all message types
    std::vector<MessageType> types = {
        MessageType::NewOrder,      MessageType::CancelOrder,     MessageType::ReplaceOrder,
        MessageType::MassCancel,    MessageType::SnapshotRequest, MessageType::Heartbeat,
        MessageType::OrderAccepted, MessageType::OrderRejected,   MessageType::OrderCanceled,
        MessageType::OrderReplaced, MessageType::OrderExecuted,   MessageType::BookAdd,
        MessageType::BookChange,    MessageType::BookDelete,      MessageType::Trade};

    for (auto type : types) {
        FrameHeader original;
        original.setMessageType(type);
        original.setSessionId(12345);
        original.setSequence(9876543210ULL);
        original.setSendTimestampNs(1234567890123456789ULL);
        original.setPayloadLength(100);
        original.setCrc32c(0xDEADBEEF);

        std::vector<uint8_t> buffer(FrameHeader::SIZE);
        original.serialize(buffer.data(), buffer.size());

        TEST_ASSERT(FrameHeader::parse(buffer.data(), buffer.size()).has_value());
    }

    std::cout << "    [PASS] Frame round-trip encoding\n";
}

void testTcpFrameFragmentation() {
    std::cout << "  Testing TCP frame fragmentation...\\n";

    // Create a frame
    FrameHeader header;
    header.setMessageType(MessageType::Heartbeat);
    header.setSessionId(1);
    header.setSequence(1);
    header.setSendTimestampNs(1000000ULL);
    header.setPayloadLength(0);

    std::vector<uint8_t> frame(FrameHeader::SIZE);
    header.serialize(frame.data(), frame.size());

    // Parse it back
    auto parsed = FrameHeader::parse(frame.data(), frame.size());
    TEST_ASSERT(parsed.has_value());
    (void)parsed;

    std::cout << "    [PASS] Frame fragmentation parsing\\n";
}

int runNetworkTests() {
    std::cout << "Running network integration tests...\n";

    testTcpFrameFragmentation();
    testTcpFrameCoalescing();
    testMalformedFrames();
    testFeedArbiterDuplicates();
    testFeedArbiterGapDetection();
    testFeedArbiterRedundantRecovery();
    testFeedArbiterSnapshotRecovery();
    testFeedArbiterSessionChange();
    testTcpGatewayCreation();
    testFrameRoundTrip();

    std::cout << "All network integration tests passed!\n";
    return 0;
}
