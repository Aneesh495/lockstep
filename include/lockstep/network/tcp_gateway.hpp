#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "lockstep/common/types.hpp"
#include "lockstep/concurrency/spsc_ring.hpp"
#include "lockstep/protocol/frame.hpp"

namespace lockstep {

// TCP gateway for order entry
// Single thread owns all client connections
// Parses frames and produces commands for SPSC ring

struct ClientConnection {
    int socketFd = -1;
    ClientId clientId = INVALID_CLIENT_ID;
    std::uint64_t nextClientSeq = 1;
    std::uint64_t highWaterMark = 0;
    bool connected = false;
    std::vector<std::uint8_t> readBuffer;
};

struct CommandMessage {
    ClientId clientId = INVALID_CLIENT_ID;
    MessageType type = MessageType::Heartbeat;
    std::vector<std::uint8_t> payload;
    std::uint64_t clientSeq = 0;
    Timestamp timestamp = 0;
};

struct ResponseMessage {
    ClientId clientId = INVALID_CLIENT_ID;
    MessageType type = MessageType::Heartbeat;
    std::vector<std::uint8_t> payload;
    std::uint64_t engineSeq = 0;
};

class TcpGateway {
public:
    explicit TcpGateway(std::uint16_t port, std::uint32_t maxClients = 100);
    ~TcpGateway();
    
    // Start/stop gateway
    bool start();
    void stop();
    
    // Command queue (producer)
    SpscRing<CommandMessage>& commandQueue() { return commandQueue_; }
    
    // Response queue (consumer)
    SpscRing<ResponseMessage>& responseQueue() { return responseQueue_; }
    
    bool isRunning() const { return running_; }

private:
    void run();
    void acceptClient();
    void handleClient(ClientConnection& client);
    bool parseFrame(ClientConnection& client);
    void sendResponse(ClientConnection& client, const ResponseMessage& response);
    
    std::uint16_t port_;
    std::uint32_t maxClients_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
    
    std::vector<ClientConnection> clients_;
    
    SpscRing<CommandMessage> commandQueue_;
    SpscRing<ResponseMessage> responseQueue_;
};

} // namespace lockstep
