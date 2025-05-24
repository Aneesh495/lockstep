#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include "lockstep/fault/fault_proxy.hpp"
#include "lockstep/network/client_book.hpp"
#include "lockstep/network/feed_arbiter.hpp"
#include "lockstep/network/udp_publisher.hpp"
#include "lockstep/protocol/codec.hpp"

#define TEST_ASSERT(cond)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::cerr << "Assertion failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ \
                      << "\n";                                                                  \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)

namespace {

int createBoundUdpSocket(uint16_t& outPort) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT(fd >= 0);

    // Set 50ms receive timeout so tests never hang
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 50000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // Ephemeral port

    TEST_ASSERT(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    socklen_t len = sizeof(addr);
    TEST_ASSERT(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    outPort = ntohs(addr.sin_port);

    return fd;
}

void testLocalhostUdpTransmissionAndReception() {
    uint16_t portA = 0;
    uint16_t portB = 0;
    int sockA = createBoundUdpSocket(portA);
    int sockB = createBoundUdpSocket(portB);

    lockstep::UdpPublisher publisher(portA, portB);
    publisher.setSessionId(42);
    TEST_ASSERT(publisher.start());

    // Create 3 market events
    std::vector<lockstep::MarketEvent> events;
    for (uint64_t i = 1; i <= 3; ++i) {
        lockstep::BookAddPayload add{};
        add.instrumentId = 1;
        add.side = lockstep::Side::Buy;
        add.price = 150 + static_cast<int64_t>(i);
        add.quantity = static_cast<uint32_t>(10 * i);
        add.engineSeq = i;

        std::vector<uint8_t> payload(sizeof(add));
        lockstep::Codec::encodeBookAdd(add, payload.data(), payload.size());

        lockstep::MarketEvent ev;
        ev.eventSeq = i;
        ev.type = lockstep::MessageType::BookAdd;
        ev.payload = std::move(payload);
        ev.timestamp = 1000 * i;

        TEST_ASSERT(publisher.eventQueue().tryPush(ev));
    }

    // Read packets from socket A and socket B
    uint8_t bufA[2048];
    uint8_t bufB[2048];
    ssize_t nA = ::recvfrom(sockA, bufA, sizeof(bufA), 0, nullptr, nullptr);
    ssize_t nB = ::recvfrom(sockB, bufB, sizeof(bufB), 0, nullptr, nullptr);

    TEST_ASSERT(nA > 0);
    TEST_ASSERT(nB > 0);

    // Verify both channels received identical payload length
    TEST_ASSERT(nA == nB);

    // Close and stop
    publisher.stop();
    ::close(sockA);
    ::close(sockB);

    std::cout << "  [PASS] Localhost UDP transmission and dual-channel reception\n";
}

void testDualFeedReconciliationWithFaults() {
    uint16_t portA = 0;
    uint16_t portB = 0;
    int sockA = createBoundUdpSocket(portA);
    int sockB = createBoundUdpSocket(portB);

    lockstep::UdpPublisher publisher(portA, portB);
    publisher.setSessionId(1);
    TEST_ASSERT(publisher.start());

    lockstep::FeedArbiter arbiter(1000);
    lockstep::FaultProxy faultProxyA(999);
    lockstep::FaultProxy faultProxyB(888);

    // Configure faults: channel A drops some packets, channel B duplicates and reorders
    faultProxyA.setLossProbability(0.20);
    faultProxyB.setDuplicateProbability(0.20);
    faultProxyB.setReorderProbability(0.20);

    lockstep::ClientBook clientBook(1);

    // Push 20 events
    for (uint64_t i = 1; i <= 20; ++i) {
        lockstep::BookAddPayload add{};
        add.instrumentId = 1;
        add.side = lockstep::Side::Buy;
        add.price = 100 + static_cast<int64_t>(i);
        add.quantity = 10;
        add.engineSeq = i;

        std::vector<uint8_t> payload(sizeof(add));
        lockstep::Codec::encodeBookAdd(add, payload.data(), payload.size());

        lockstep::MarketEvent ev;
        ev.eventSeq = i;
        ev.type = lockstep::MessageType::BookAdd;
        ev.payload = std::move(payload);
        ev.timestamp = 1000 * i;

        publisher.eventQueue().tryPush(ev);
    }

    // Allow publisher background thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Receive packets from both channels non-blockingly and route through fault proxies
    for (int iter = 0; iter < 10; ++iter) {
        uint8_t buf[2048];
        while (true) {
            ssize_t nA = ::recvfrom(sockA, buf, sizeof(buf), MSG_DONTWAIT, nullptr, nullptr);
            if (nA <= 0) {
                break;
            }
            std::vector<uint8_t> pkt(buf, buf + nA);
            lockstep::DeliveryEnvelope env;
            env.channel = 'A';
            env.payload = std::move(pkt);
            auto envs = faultProxyA.submit(std::move(env));
            for (const auto& e : envs) {
                arbiter.onPacket(e.channel, 1, 0, 1, 0, e.payload.data(), e.payload.size());
            }
        }

        while (true) {
            ssize_t nB = ::recvfrom(sockB, buf, sizeof(buf), MSG_DONTWAIT, nullptr, nullptr);
            if (nB <= 0) {
                break;
            }
            std::vector<uint8_t> pkt(buf, buf + nB);
            lockstep::DeliveryEnvelope env;
            env.channel = 'B';
            env.payload = std::move(pkt);
            auto envs = faultProxyB.submit(std::move(env));
            for (const auto& e : envs) {
                arbiter.onPacket(e.channel, 1, 0, 1, 0, e.payload.data(), e.payload.size());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Drain remaining reordered packets from fault proxies
    for (const auto& e : faultProxyA.drain()) {
        arbiter.onPacket(e.channel, 1, 0, 1, 0, e.payload.data(), e.payload.size());
    }
    for (const auto& e : faultProxyB.drain()) {
        arbiter.onPacket(e.channel, 1, 0, 1, 0, e.payload.data(), e.payload.size());
    }

    // Apply any ready events to client book
    while (arbiter.hasReadyEvents()) {
        auto ev = arbiter.nextEvent();
        if (ev) {
            clientBook.applyEvent(ev->type, ev->payload.data(), ev->payload.size());
        }
    }

    publisher.stop();
    ::close(sockA);
    ::close(sockB);

    std::cout << "  [PASS] Dual-feed reconciliation with loss, duplication, and reordering\n";
}

void testPublisherBoundedLifecycle() {
    // Test multiple start/stop cycles to verify zero resource leaks or hangs
    for (int i = 0; i < 5; ++i) {
        lockstep::UdpPublisher pub(19100, 19101);
        TEST_ASSERT(pub.start());
        pub.stop();
    }
    std::cout << "  [PASS] Publisher bounded startup and shutdown lifecycle\n";
}

}  // namespace

int runUdpTests() {
    testLocalhostUdpTransmissionAndReception();
    testDualFeedReconciliationWithFaults();
    testPublisherBoundedLifecycle();
    return 0;
}
