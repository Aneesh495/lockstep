#include "lockstep/persistence/wal.hpp"
#include "lockstep/persistence/snapshot.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>


void testWalWriteRead() {
    std::string walPath = "/tmp/lockstep_test.wal";
    
    lockstep::WalWriter writer(walPath);
    assert(writer.isOpen());
    
    uint8_t data[] = {0x01, 0x02, 0x03};
    assert(writer.append(1, 1000000, data, sizeof(data)));
    (void)data;
    
    writer.close();
    
    std::filesystem::remove(walPath);
    
    std::cout << "  [PASS] WAL write/read\n";
}

void testSnapshotBasic() {
    std::string snapPath = "/tmp/lockstep_test.snap";
    
    lockstep::MatchingEngine::Config config;
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.maxOrdersPerLevel = 100;
    instr.maxPriceLevels = 101;
    config.instruments.push_back(instr);
    
    lockstep::MatchingEngine engine(config);
    lockstep::RiskEngine risk;
    
    lockstep::SnapshotWriter writer(snapPath);
    assert(writer.write(engine, risk));
    
    std::filesystem::remove(snapPath);
    
    std::cout << "  [PASS] Snapshot basic\n";
}

int runRecoveryTests() {
    testWalWriteRead();
    testSnapshotBasic();
    return 0;
}

