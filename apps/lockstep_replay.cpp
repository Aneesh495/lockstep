#include "lockstep/persistence/wal.hpp"
#include "lockstep/engine/matching_engine.hpp"
#include <iostream>
#include <fstream>

using namespace lockstep;

int main(int argc, char** argv) {
    std::cout << "Lockstep WAL Replay\n";
    std::cout << "===================\n\n";
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <wal_file>\n";
        return 1;
    }
    
    std::string walPath = argv[1];
    
    WalReader reader(walPath);
    if (!reader.isOpen()) {
        std::cerr << "Failed to open WAL: " << walPath << "\n";
        return 1;
    }
    
    std::cout << "Replaying WAL: " << walPath << "\n\n";
    
    uint64_t recordCount = 0;
    uint64_t lastCommandSeq = 0;
    uint64_t bytesProcessed = 0;
    
    while (true) {
        auto record = reader.readNext();
        if (!record) break;
        
        recordCount++;
        lastCommandSeq = record->commandSeq;
        bytesProcessed += record->payloadLength + 28;
        
        if (recordCount % 10000 == 0) {
            std::cout << "  Processed " << recordCount << " records...\n";
        }
    }
    
    std::cout << "\nReplay Statistics:\n";
    std::cout << "  Total records: " << recordCount << "\n";
    std::cout << "  Last command sequence: " << lastCommandSeq << "\n";
    std::cout << "  Bytes processed: " << bytesProcessed << "\n";
    
    return 0;
}
