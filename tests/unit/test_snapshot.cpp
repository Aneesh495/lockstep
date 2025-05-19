#include <cstdlib>
#include "lockstep/persistence/snapshot.hpp"

#define TEST_ASSERT(cond)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::cerr << "Assertion failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ \
                      << "\n";                                                                  \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)
#include <filesystem>
#include <iostream>

namespace {

void testSnapshotEmptyEngine() {
    std::string path = "/tmp/lockstep_unit_empty.snap";
    std::filesystem::remove(path);

    lockstep::MatchingEngine::Config config;
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 10;
    instr.maxPrice = 100;
    instr.tickSize = 1;
    instr.maxOrdersPerLevel = 100;
    instr.maxPriceLevels = 100;
    config.instruments.push_back(instr);

    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;

    // Write snapshot of initial engine state
    lockstep::SnapshotWriter writer(path);
    TEST_ASSERT(writer.write(engine, risk));

    // Read back
    lockstep::MatchingEngine restored(config);
    lockstep::RiskEngine restoredRisk;
    lockstep::SnapshotReader reader(path);
    TEST_ASSERT(reader.read(restored, restoredRisk));

    TEST_ASSERT(restored.currentCommandSeq() == engine.currentCommandSeq());
    TEST_ASSERT(restored.currentEventSeq() == engine.currentEventSeq());
    TEST_ASSERT(restored.computeStateDigest() == engine.computeStateDigest());
    TEST_ASSERT(restoredRisk.computeDigest() == risk.computeDigest());

    std::filesystem::remove(path);
    std::cout << "  [PASS] Snapshot empty engine\n";
}

void testSnapshotNonExistentFile() {
    lockstep::MatchingEngine::Config config;
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;

    lockstep::SnapshotReader reader("/tmp/does_not_exist_98234.snap");
    TEST_ASSERT(!reader.read(engine, risk));
    TEST_ASSERT(!reader.error().empty());
    std::cout << "  [PASS] Snapshot non-existent file handling\n";
}

}  // namespace

int runSnapshotTests() {
    testSnapshotEmptyEngine();
    testSnapshotNonExistentFile();
    return 0;
}
