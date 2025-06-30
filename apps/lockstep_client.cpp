#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <iostream>
#include <string>
#include <vector>
#include "lockstep/common/crc32c.hpp"
#include "lockstep/protocol/codec.hpp"
#include "lockstep/protocol/frame.hpp"

using namespace lockstep;
namespace {
bool sendAll(int socket, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        auto n = ::send(socket, data + offset, size - offset, flags);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}
bool receiveAll(int socket, uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        auto n = ::recv(socket, data + offset, size - offset, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}
bool exchange(int socket, MessageType type, uint64_t sequence, const uint8_t* payload,
              size_t size) {
    FrameHeader header;
    header.setMessageType(type);
    header.setSessionId(1);
    header.setSequence(sequence);
    header.setPayloadLength(static_cast<uint32_t>(size));
    std::vector<uint8_t> frame(FrameHeader::SIZE + size);
    header.serialize(frame.data(), frame.size());
    std::copy(payload, payload + size, frame.begin() + FrameHeader::SIZE);
    header.setCrc32c(Crc32C::compute(frame.data(), frame.size()));
    header.serialize(frame.data(), frame.size());
    if (!sendAll(socket, frame.data(), frame.size()))
        return false;
    std::array<uint8_t, FrameHeader::SIZE> bytes{};
    if (!receiveAll(socket, bytes.data(), bytes.size()))
        return false;
    auto response = FrameHeader::parse(bytes.data(), bytes.size());
    if (!response)
        return false;
    std::vector<uint8_t> body(response->payloadLength());
    if (!receiveAll(socket, body.data(), body.size()))
        return false;
    if (response->messageType() == MessageType::OrderAccepted) {
        auto accepted = Codec::decodeOrderAccepted(body.data(), body.size());
        if (!accepted || body.size() != sizeof(*accepted))
            return false;
        std::cout << "Accepted command " << accepted->engineSeq << '\n';
    } else if (response->messageType() == MessageType::OrderRejected) {
        auto rejected = Codec::decodeOrderRejected(body.data(), body.size());
        if (!rejected || body.size() != sizeof(*rejected))
            return false;
        std::cout << "Rejected command " << rejected->engineSeq << ": "
                  << static_cast<uint16_t>(rejected->reason) << '\n';
    } else {
        return false;
    }
    return true;
}
}  // namespace
int main(int argc, char** argv) {
    uint16_t port = 9999;
    if (argc == 2) {
        try {
            const auto value = std::stoul(argv[1]);
            if (value == 0 || value > 65535)
                return 1;
            port = static_cast<uint16_t>(value);
        } catch (const std::exception&) {
            return 1;
        }
    } else if (argc != 1) {
        return 1;
    }
    int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0)
        return 1;
#ifdef SO_NOSIGPIPE
    int noSignal = 1;
    if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal)) != 0) {
        ::close(socket);
        return 1;
    }
#endif
    timeval timeout{3, 0};
    if (::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0 ||
        ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ::close(socket);
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1 ||
        ::connect(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Connection failed\n";
        ::close(socket);
        return 1;
    }
    std::cout << "Connected on port " << port << "; commands: new, cancel, quit\n";
    uint64_t sequence = 0, orderId = 0;
    std::string command;
    bool ok = true;
    while (std::cin >> command && command != "quit") {
        std::array<uint8_t, 128> payload{};
        size_t size = 0;
        MessageType type;
        if (command == "new") {
            NewOrderPayload order{};
            order.clientId = 1;
            order.orderId = ++orderId;
            order.instrumentId = 1;
            order.side = Side::Buy;
            order.tif = TimeInForce::GTC;
            order.price = 150;
            order.quantity = 100;
            order.clientSeq = ++sequence;
            size = Codec::encodeNewOrder(order, payload.data(), payload.size());
            type = MessageType::NewOrder;
        } else if (command == "cancel") {
            CancelOrderPayload cancel{};
            cancel.clientId = 1;
            cancel.orderId = orderId;
            cancel.clientSeq = ++sequence;
            size = Codec::encodeCancelOrder(cancel, payload.data(), payload.size());
            type = MessageType::CancelOrder;
        } else {
            std::cerr << "Unknown command\n";
            continue;
        }
        if (!size || !exchange(socket, type, sequence, payload.data(), size)) {
            std::cerr << "Command transmission or response failed\n";
            ok = false;
            break;
        }
    }
    ::close(socket);
    return ok ? 0 : 1;
}
