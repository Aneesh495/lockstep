#pragma once

#include <cstdint>
#include <string>
#include "lockstep/common/types.hpp"

namespace lockstep {

// Client for requesting snapshots via TCP

class SnapshotClient {
public:
    SnapshotClient(const std::string& host, std::uint16_t port);
    
    bool connect();
    void disconnect();
    
    bool requestSnapshot(std::uint64_t fromSeq);
    
    bool isConnected() const { return connected_; }

private:
    std::string host_;
    std::uint16_t port_;
    int socketFd_ = -1;
    bool connected_ = false;
};

} // namespace lockstep
