#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "lockstep/network/client_book.hpp"
#include "lockstep/network/feed_arbiter.hpp"
#include "lockstep/network/udp_publisher.hpp"
#include "lockstep/protocol/codec.hpp"
#define TEST_ASSERT(c)                                     \
    do {                                                   \
        if (!(c)) {                                        \
            std::cerr << #c << " at " << __LINE__ << "\n"; \
            std::abort();                                  \
        }                                                  \
    } while (0)
namespace {
int bindSocket(uint16_t& port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    socklen_t len = sizeof(addr);
    TEST_ASSERT(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    port = ntohs(addr.sin_port);
    return fd;
}
}  // namespace
int runUdpTests() {
    uint16_t a, b;
    int fa = bindSocket(a), fb = bindSocket(b);
    lockstep::UdpPublisher publisher(a, b);
    publisher.setSessionId(42);
    for (uint64_t i = 1; i <= 240; ++i) {
        lockstep::BookAddPayload p{};
        p.instrumentId = 1;
        p.side = lockstep::Side::Buy;
        p.price = 100 + static_cast<int64_t>(i % 21);
        p.quantity = 10;
        p.engineSeq = i;
        lockstep::MarketEvent e;
        e.eventSeq = i;
        e.type = lockstep::MessageType::BookAdd;
        e.payload.resize(sizeof(p));
        TEST_ASSERT(lockstep::Codec::encodeBookAdd(p, e.payload.data(), e.payload.size()) ==
                    sizeof(p));
        TEST_ASSERT(publisher.eventQueue().tryPush(e));
    }
    TEST_ASSERT(publisher.start());
    lockstep::FaultProxy pa(19), pb(41);
    pa.setChannelOutage('A', true);
    pb.setDuplicateProbability(1);
    pb.setReorderProbability(0.5);
    lockstep::FeedArbiter arbiter(1000);
    arbiter.setExpectedSeq(1);
    lockstep::ClientBook book(1);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    uint64_t counts[2] = {0, 0};
    while ((counts[0] < 12 || counts[1] < 12) && std::chrono::steady_clock::now() < deadline) {
        for (int c = 0; c < 2; ++c) {
            uint8_t buf[2048];
            auto n = ::recv(c == 0 ? fa : fb, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0) {
                ++counts[c];
                lockstep::DeliveryEnvelope e;
                e.channel = c == 0 ? 'A' : 'B';
                e.payload.assign(buf, buf + n);
                for (const auto& env : (c == 0 ? pa : pb).submit(std::move(e)))
                    arbiter.onEnvelope(env);
            }
        }
        std::this_thread::yield();
    }
    for (const auto& e : pb.drain())
        arbiter.onEnvelope(e);
    while (auto e = arbiter.nextEvent())
        book.applyEvent(e->type, e->payload.data(), e->payload.size());
    publisher.stop();
    ::close(fa);
    ::close(fb);
    TEST_ASSERT(counts[0] == 12 && counts[1] == 12);
    TEST_ASSERT(book.appliedEvents() == 240);
    for (int64_t price = 100; price <= 120; ++price) {
        uint32_t expected = 0;
        for (uint64_t i = 1; i <= 240; ++i)
            if (100 + static_cast<int64_t>(i % 21) == price)
                expected += 10;
        TEST_ASSERT(book.bidQuantity(price) == expected);
    }
    TEST_ASSERT(arbiter.duplicatesDiscarded() > 0);
    TEST_ASSERT(pa.packetsOutage() == 12);
    TEST_ASSERT(pb.packetsReordered() > 0);
    std::cout
        << "  [PASS] Real UDP: 24 packets, 240 client events, A outage, B duplicate/reorder\n";
    return 0;
}
