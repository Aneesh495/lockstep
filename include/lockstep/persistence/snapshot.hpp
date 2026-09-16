#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "lockstep/common/types.hpp"
#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/risk/risk_engine.hpp"

namespace lockstep {

// Snapshot format:
// - Header: magic, version, timestamp, command_seq, event_seq
// - Instruments: count + configs
// - Orders: count + serialized orders
// - Risk state: count + client states
// - Footer: CRC32C

struct SnapshotHeader {
    std::uint32_t magic = 0x534E4150; // "SNAP"
    std::uint8_t version = 1;
    std::uint8_t reserved[3] = {0};
    std::uint64_t timestamp = 0;
    std::uint64_t commandSeq = 0;
    std::uint64_t eventSeq = 0;
    std::uint64_t coveredWalSeq = 0;
    std::uint32_t instrumentCount = 0;
    std::uint32_t orderCount = 0;
    std::uint32_t clientCount = 0;
};

class SnapshotWriter {
public:
    explicit SnapshotWriter(const std::string& path);
    
    bool write(const MatchingEngine& engine, const RiskEngine& risk);
    
    std::string error() const { return error_; }

private:
    std::string path_;
    std::string error_;
};

class SnapshotReader {
public:
    explicit SnapshotReader(const std::string& path);
    
    bool read(MatchingEngine& engine, RiskEngine& risk);
    
    std::uint64_t commandSeq() const { return header_.commandSeq; }
    std::uint64_t eventSeq() const { return header_.eventSeq; }
    std::uint64_t coveredWalSeq() const { return header_.coveredWalSeq; }
    
    std::string error() const { return error_; }

private:
    std::string path_;
    SnapshotHeader header_;
    std::string error_;
};

} // namespace lockstep
