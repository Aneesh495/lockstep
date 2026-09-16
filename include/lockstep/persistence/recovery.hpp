#pragma once

#include <cstdint>
#include <string>
#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/risk/risk_engine.hpp"
#include "lockstep/persistence/wal.hpp"
#include "lockstep/persistence/snapshot.hpp"

namespace lockstep {

// Recovery manager
// Loads snapshot and replays WAL to restore engine state

class RecoveryManager {
public:
    RecoveryManager(const std::string& snapshotPath, const std::string& walPath);
    
    // Recover engine state
    bool recover(MatchingEngine& engine, RiskEngine& risk);
    
    // Get recovery stats
    std::uint64_t recoveredCommandSeq() const { return recoveredCommandSeq_; }
    std::uint64_t recoveredEventSeq() const { return recoveredEventSeq_; }
    std::uint32_t replayedRecords() const { return replayedRecords_; }
    
    std::string error() const { return error_; }

private:
    std::string snapshotPath_;
    std::string walPath_;
    
    std::uint64_t recoveredCommandSeq_ = 0;
    std::uint64_t recoveredEventSeq_ = 0;
    std::uint32_t replayedRecords_ = 0;
    std::string error_;
};

} // namespace lockstep
