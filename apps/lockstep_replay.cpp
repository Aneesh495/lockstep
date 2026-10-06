#include <filesystem>
#include <iostream>
#include <string>
#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/persistence/recovery.hpp"
#include "lockstep/risk/risk_engine.hpp"
using namespace lockstep;
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <exchange_wal> [exchange_snapshot]\n";
        return 1;
    }
    try {
        const std::string wal = argv[1], snapshot = argc == 3 ? argv[2] : "";
        if (!std::filesystem::is_regular_file(wal) ||
            (!snapshot.empty() && !std::filesystem::is_regular_file(snapshot))) {
            std::cerr << "Missing replay input\n";
            return 1;
        }
        // Use the standalone exchange's instrument contract. Snapshot loading
        // rejects inputs produced under a different instrument configuration.
        MatchingEngine::Config config;
        config.useReferenceBook = false;
        InstrumentConfig instrument;
        instrument.id = 1;
        instrument.minPrice = 100;
        instrument.maxPrice = 1000;
        config.instruments.push_back(instrument);
        MatchingEngine engine(config);
        RiskEngine risk;
        RecoveryManager recovery(snapshot, wal);
        if (!recovery.recover(engine, risk)) {
            std::cerr << "Replay failed: " << recovery.error() << '\n';
            return 1;
        }
        std::string error;
        if (!engine.checkInvariants(error)) {
            std::cerr << "Recovered state invalid: " << error << '\n';
            return 1;
        }
        std::cout << "Applied WAL commands: " << recovery.replayedRecords() << '\n'
                  << "Recovered command sequence: " << recovery.recoveredCommandSeq() << '\n'
                  << "Valid WAL bytes: " << recovery.validWalBytes() << '\n'
                  << "Resting orders: " << engine.totalOrderCount() << '\n'
                  << "Matches: " << engine.totalMatchCount() << '\n'
                  << "Engine digest: " << engine.computeStateDigest() << '\n'
                  << "Risk digest: " << risk.computeDigest() << '\n'
                  << "Recoverable incomplete final tail: "
                  << (recovery.finalTailIncomplete() ? "yes" : "no") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Replay failed: " << error.what() << '\n';
        return 1;
    }
}
