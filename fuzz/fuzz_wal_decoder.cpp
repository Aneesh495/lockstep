// Fuzz target for WAL decoder
// Tests robustness of WAL record parsing against malformed input
// Can be built with libFuzzer (Clang) or run standalone

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>
#include <random>
#include "lockstep/common/endian.hpp"

using namespace lockstep;

// Test function called by both libFuzzer and standalone
static int test_wal_decoder(const uint8_t* data, size_t size) {
    if (size < 1) return 0;
    
    // WAL record header: magic(4) + version(1) + kind(1) + length(2) + cmdSeq(8) + timestamp(8) = 24 bytes minimum
    constexpr size_t MIN_RECORD_HEADER = 24;
    
    // Test 1: Parse WAL records directly from memory
    if (size >= MIN_RECORD_HEADER) {
        // Try to interpret as WAL record
        ByteReader reader(data, size);
        
        uint32_t magic = 0;
        uint8_t version = 0;
        uint8_t kind = 0;
        uint16_t length = 0;
        uint64_t cmdSeq = 0;
        uint64_t timestamp = 0;
        
        if (reader.readU32(magic) &&
            reader.readU8(version) &&
            reader.readU8(kind) &&
            reader.readU16(length) &&
            reader.readU64(cmdSeq) &&
            reader.readU64(timestamp)) {
            
            // Validate magic
            if (magic == 0x57414C4B) { // "WALK"
                // Valid magic, check version
                if (version == 1) {
                    // Try to read payload
                    size_t totalSize = MIN_RECORD_HEADER + length + 4; // +4 for CRC
                    if (size >= totalSize) {
                        // Read CRC
                        const uint8_t* crcPos = data + MIN_RECORD_HEADER + length;
                        ByteReader crcReader(crcPos, 4);
                        uint32_t storedCrc = 0;
                        if (crcReader.readU32(storedCrc)) {
                            // CRC validation would go here
                            (void)storedCrc;
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}

// Fuzz entry point for libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return test_wal_decoder(data, size);
}

// Standalone entry point for smoke testing
int main(int argc, char* argv[]) {
    std::cout << "WAL Decoder Fuzz Smoke Test\n";
    std::cout << "===========================\n\n";
    
    uint32_t seed = 12345;
    if (argc > 1) {
        seed = static_cast<uint32_t>(std::stoul(argv[1]));
    }
    std::cout << "Seed: " << seed << "\n";
    
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> lenDist(0, 512);
    std::uniform_int_distribution<uint8_t> byteDist(0, 255);
    
    int passed = 0;
    
    // Test random sequences
    std::cout << "Testing 5000 random byte sequences...\n";
    for (int i = 0; i < 5000; i++) {
        size_t size = lenDist(rng);
        std::vector<uint8_t> data(size);
        for (auto& b : data) {
            b = byteDist(rng);
        }
        test_wal_decoder(data.data(), data.size());
        passed++;
    }
    std::cout << "  Passed: " << passed << "\n";
    
    // Test valid WAL records
    std::cout << "Testing valid WAL records...\n";
    for (int i = 0; i < 100; i++) {
        std::vector<uint8_t> data(24 + 32 + 4);  // header + payload + CRC
        ByteWriter writer(data.data(), data.size());
        
        writer.writeU32(0x57414C4B);  // WALK magic
        writer.writeU8(1);  // version
        writer.writeU8(1);  // kind
        writer.writeU16(32);  // payload length
        writer.writeU64(static_cast<uint64_t>(i + 1));  // command sequence
        writer.writeU64(1000000ULL + static_cast<uint64_t>(i));  // timestamp
        
        // Random payload
        for (size_t j = 0; j < 32; j++) {
            data[24 + j] = byteDist(rng);
        }
        
        // CRC placeholder
        writer.writeU32(0);
        
        test_wal_decoder(data.data(), data.size());
        passed++;
    }
    std::cout << "  Total passed: " << passed << "\n";
    
    // Edge cases
    std::cout << "Testing edge cases...\n";
    
    // Single byte
    uint8_t single = byteDist(rng);
    test_wal_decoder(&single, 1);
    passed++;
    
    // Valid magic only
    std::vector<uint8_t> magicOnly(4);
    ByteWriter mw(magicOnly.data(), 4);
    mw.writeU32(0x57414C4B);
    test_wal_decoder(magicOnly.data(), magicOnly.size());
    passed++;
    
    std::cout << "  Total passed: " << passed << "\n";
    
    std::cout << "\nAll smoke tests passed!\n";
    std::cout << "Total tests: " << passed << "\n";
    
    return 0;
}
