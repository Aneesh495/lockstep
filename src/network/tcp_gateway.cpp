#include "lockstep/network/tcp_gateway.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace lockstep {

TcpGateway::TcpGateway(std::uint16_t port, std::uint32_t maxClients)
    : port_(port)
    , maxClients_(maxClients)
    , commandQueue_(maxClients * 100)
    , responseQueue_(maxClients * 100)
{
    clients_.reserve(maxClients);
}

TcpGateway::~TcpGateway() {
    stop();
}

bool TcpGateway::start() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        return false;
    }
    
    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listenFd_);
        return false;
    }
    
    if (listen(listenFd_, 10) < 0) {
        ::close(listenFd_);
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(listenFd_, F_GETFL, 0);
    fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);
    
    running_ = true;
    thread_ = std::thread(&TcpGateway::run, this);
    
    return true;
}

void TcpGateway::stop() {
    running_ = false;
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    
    for (auto& client : clients_) {
        if (client.socketFd >= 0) {
            ::close(client.socketFd);
        }
    }
    clients_.clear();
}

void TcpGateway::run() {
    while (running_) {
        // Accept new connections
        acceptClient();
        
        // Poll existing clients
        std::vector<pollfd> pollfds;
        pollfds.push_back({listenFd_, POLLIN, 0});
        
        for (auto& client : clients_) {
            if (client.connected) {
                pollfds.push_back({client.socketFd, POLLIN, 0});
            }
        }
        
        if (poll(&pollfds[0], static_cast<nfds_t>(pollfds.size()), 10) > 0) {
            // Handle client data
            for (std::size_t i = 1; i < pollfds.size(); ++i) {
                if (pollfds[i].revents & POLLIN) {
                    handleClient(clients_[i - 1]);
                }
            }
        }
        
        // Send responses
        ResponseMessage response;
        while (responseQueue_.tryPop(response)) {
            for (auto& client : clients_) {
                if (client.clientId == response.clientId && client.connected) {
                    sendResponse(client, response);
                }
            }
        }
    }
}

void TcpGateway::acceptClient() {
    sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    
    int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    if (clientFd < 0) {
        return;
    }
    
    if (clients_.size() >= maxClients_) {
        ::close(clientFd);
        return;
    }
    
    // Set TCP_NODELAY for low latency
    int flag = 1;
    setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Set non-blocking
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);
    
    ClientConnection client;
    client.socketFd = clientFd;
    client.clientId = static_cast<ClientId>(clients_.size() + 1);
    client.connected = true;
    client.readBuffer.reserve(65536);
    
    clients_.push_back(client);
}

void TcpGateway::handleClient(ClientConnection& client) {
    std::uint8_t buffer[4096];
    
    ssize_t bytesRead = read(client.socketFd, buffer, sizeof(buffer));
    if (bytesRead <= 0) {
        client.connected = false;
        ::close(client.socketFd);
        client.socketFd = -1;
        return;
    }
    
    client.readBuffer.insert(client.readBuffer.end(), buffer, buffer + bytesRead);
    
    // Try to parse complete frames
    while (parseFrame(client)) {
    }
}

bool TcpGateway::parseFrame(ClientConnection& client) {
    if (client.readBuffer.size() < FRAME_HEADER_SIZE) {
        return false;
    }
    
    auto header = FrameHeader::parse(client.readBuffer.data(), client.readBuffer.size());
    if (!header) {
        return false;
    }
    
    std::size_t totalSize = header->totalSize();
    if (client.readBuffer.size() < totalSize) {
        return false;
    }
    
    // Extract command
    CommandMessage cmd;
    cmd.clientId = client.clientId;
    cmd.type = header->messageType();
    cmd.timestamp = header->sendTimestampNs();
    
    if (header->payloadLength() > 0) {
        cmd.payload.assign(
            client.readBuffer.begin() + static_cast<std::ptrdiff_t>(FRAME_HEADER_SIZE),
            client.readBuffer.begin() + static_cast<std::ptrdiff_t>(totalSize)
        );
    }
    
    // Try to push to queue
    if (!commandQueue_.tryPush(cmd)) {
        // Queue full - drop
    }
    
    // Remove processed bytes
    client.readBuffer.erase(client.readBuffer.begin(), client.readBuffer.begin() + static_cast<std::ptrdiff_t>(totalSize));
    
    return true;
}

void TcpGateway::sendResponse(ClientConnection& client, const ResponseMessage& response) {
    FrameHeader header;
    header.setMessageType(response.type);
    header.setSessionId(1); // TODO: session management
    header.setSequence(response.engineSeq);
    header.setSendTimestampNs(0);
    header.setPayloadLength(static_cast<std::uint32_t>(response.payload.size()));
    
    std::vector<std::uint8_t> buffer(FRAME_HEADER_SIZE + response.payload.size());
    header.serialize(buffer.data(), buffer.size());
    std::copy(response.payload.begin(), response.payload.end(), 
              buffer.begin() + FRAME_HEADER_SIZE);
    
    write(client.socketFd, buffer.data(), buffer.size());
}

} // namespace lockstep
