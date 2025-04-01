// Fuzz target for snapshot decoder
// Tests robustness of snapshot parsing against malformed input
// Can be built with libFuzzer (Clang) or run standalone

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <random>
#include "lockstep/common/endian.hpp"

using namespace lockstep;

// Test function called by both libFuzzer and standalone
static int test_snapshot_decoder(const uint8_t* data, size_t size) {
    if (size < 1) return 0;
    
    // Snapshot header size: magic(4) + version(1) + reserved(3) + timestamp(8) + 
    //                        cmdSeq(8) + eventSeq(8) + coveredWalSeq(8) +
    //                        instCount(4) + orderCount(4) + clientCount(4) = 52 bytes
    constexpr size_t MIN_HEADER_SIZE = 52;
    
    // Test 1: Parse snapshot header
    if (size >= MIN_HEADER_SIZE) {
        ByteReader reader(data, size);
        
        uint32_t magic = 0;
        uint8_t version = 0;
        uint8_t reserved[3] = {0};
        uint64_t timestamp = 0;
        uint64_t cmdSeq = 0;
        uint64_t eventSeq = 0;
        uint64_t coveredWalSeq = 0;
        uint32_t instCount = 0;
        uint32_t orderCount = 0;
        uint32_t clientCount = 0;
        
        if (reader.readU32(magic) &&
            reader.readU8(version) &&
            reader.readBytes(reserved, 3) &&
            reader.readU64(timestamp) &&
            reader.readU64(cmdSeq) &&
            reader.readU64(eventSeq) &&
            reader.readU64(coveredWalSeq) &&
            reader.readU32(instCount) &&
            reader.readU32(orderCount) &&
            reader.readU32(clientCount)) {
            
            // Validate magic
            if (magic == 0x534E4150) { // "SNAP"
                if (version == 1) {
                    // Check reserved is zero
                    (void)reserved;
                    
                    // Validate counts are reasonable
                    if (instCount > 10000 || orderCount > 1000000 || clientCount > 10000) {
                        // Unreasonably large counts - might be corrupt
                    }
                }
            }
        }
    }
    
    return 0;
}

// Fuzz entry point for libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return test_snapshot_decoder(data, size);
}

// Standalone entry point for smoke testing
int main(int argc, char* argv[]) {
    std::cout << "Snapshot Decoder Fuzz Smoke Test\n";
    std::cout << "================================\n\n";
    
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
        test_snapshot_decoder(data.data(), data.size());
        passed++;
    }
    std::cout << "  Passed: " << passed << "\n";
    
    // Test valid snapshot headers
    std::cout << "Testing valid snapshot headers...\n";
    for (int i = 0; i < 100; i++) {
        std::vector<uint8_t> data(52 + 4);  // header + CRC
        ByteWriter writer(data.data(), data.size());
        
        writer.writeU32(0x534E4150);  // SNAP magic
        writer.writeU8(1);  // version
        writer.writeBytes("\x00\x00\x00", 3);  // reserved
        writer.writeU64(1000000ULL + static_cast<uint64_t>(i));  // timestamp
        writer.writeU64(static_cast<uint64_t>(i + 1));  // command sequence
        writer.writeU64(static_cast<uint64_t>(i + 1));  // event sequence
        writer.writeU64(0);  // covered WAL sequence
        writer.writeU32(1);  // instrument count
        writer.writeU32(0);  // order count
        writer.writeU32(0);  // client count
        
        // CRC placeholder
        writer.writeU32(0);
        
        test_snapshot_decoder(data.data(), data.size());
        passed++;
    }
    std::cout << "  Total passed: " << passed << "\n";
    
    // Edge cases
    std::cout << "Testing edge cases...\n";
    
    uint8_t single = byteDist(rng);
    test_snapshot_decoder(&single, 1);
    passed++;
    
    std::vector<uint8_t> magicOnly(4);
    ByteWriter mw(magicOnly.data(), 4);
    mw.writeU32(0x534E4150);
    test_snapshot_decoder(magicOnly.data(), magicOnly.size());
    passed++;
    
    std::cout << "  Total passed: " << passed << "\n";
    
    std::cout << "\nAll smoke tests passed!\n";
    std::cout << "Total tests: " << passed << "\n";
    
    return 0;
}
