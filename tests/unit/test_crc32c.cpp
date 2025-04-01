#include "lockstep/common/crc32c.hpp"
#include <iostream>
#include <cassert>

namespace {

void testCheckVectors() {
    // CRC32C check vector from the spec
    const char* data = "123456789";
    uint32_t crc = lockstep::Crc32C::compute(
        reinterpret_cast<const uint8_t*>(data), 9);
    (void)crc;
    assert(crc == 0xE3069283);
    
    std::cout << "  [PASS] CRC32C check vectors\n";
}

void testIncremental() {
    const char* data = "123456789";
    
    // One-shot
    uint32_t oneShot = lockstep::Crc32C::compute(
        reinterpret_cast<const uint8_t*>(data), 9);
    (void)oneShot;
    
    // Incremental
    uint32_t incremental = 0xFFFFFFFF;
    incremental = lockstep::Crc32C::compute(incremental, 
        std::span(reinterpret_cast<const uint8_t*>(data), 9));
    incremental ^= 0xFFFFFFFF;
    
    assert(oneShot == incremental);
    assert(oneShot == 0xE3069283);
    
    std::cout << "  [PASS] CRC32C incremental update\n";
}

void testEmpty() {
    uint32_t crc = lockstep::Crc32C::compute(std::span<const uint8_t>{});
    (void)crc;
    assert(crc == 0);
    std::cout << "  [PASS] CRC32C empty input\n";
}

}

int runCrc32cTests() {
    testCheckVectors();
    testIncremental();
    testEmpty();
    return 0;
}
