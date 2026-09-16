#include "lockstep/persistence/recovery.hpp"

namespace lockstep {

RecoveryManager::RecoveryManager(const std::string& snapshotPath, const std::string& walPath)
    : snapshotPath_(snapshotPath)
    , walPath_(walPath)
{}

bool RecoveryManager::recover(MatchingEngine& engine, RiskEngine& risk) {
    // Try to load snapshot
    if (!snapshotPath_.empty()) {
        SnapshotReader reader(snapshotPath_);
        if (reader.read(engine, risk)) {
            recoveredCommandSeq_ = reader.commandSeq();
            recoveredEventSeq_ = reader.eventSeq();
        }
    }
    
    // Replay WAL
    if (!walPath_.empty()) {
        WalReader walReader(walPath_);
        if (!walReader.isOpen()) {
            error_ = "Cannot open WAL: " + walPath_;
            return false;
        }
        
        while (true) {
            auto record = walReader.readNext();
            if (!record) break;
            
            // Skip records before snapshot
            if (record->commandSeq <= recoveredCommandSeq_) {
                continue;
            }
            
            // Replay would go here - for now just count
            replayedRecords_++;
            recoveredCommandSeq_ = record->commandSeq;
        }
    }
    
    return true;
}

} // namespace lockstep
