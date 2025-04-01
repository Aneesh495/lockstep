#include "lockstep/network/snapshot_client.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace lockstep {

SnapshotClient::SnapshotClient(const std::string& host, std::uint16_t port)
    : host_(host)
    , port_(port)
{}

bool SnapshotClient::connect() {
    socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd_ < 0) {
        return false;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    
    if (::connect(socketFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }
    
    connected_ = true;
    return true;
}

void SnapshotClient::disconnect() {
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
    connected_ = false;
}

bool SnapshotClient::requestSnapshot(std::uint64_t fromSeq) {
    if (!connected_) {
        return false;
    }
    
    // Send snapshot request
    std::uint8_t buffer[16];
    std::memcpy(buffer, &fromSeq, 8);
    
    ssize_t sent = write(socketFd_, buffer, 8);
    return sent == 8;
}

} // namespace lockstep
