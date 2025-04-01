#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

namespace lockstep {

// CRC32C implementation (Castagnoli polynomial)
// Table-driven portable implementation, verified against standard check vectors

class Crc32C {
public:
    // Compute CRC32C of a byte range
    static std::uint32_t compute(std::span<const std::uint8_t> data);
    
    // Compute CRC32C with initial value (for incremental updates)
    static std::uint32_t compute(std::uint32_t init, std::span<const std::uint8_t> data);
    
    // Single-shot convenience function
    static std::uint32_t compute(const void* data, std::size_t length);
    
    // Combine two CRCs: crc(AB) = combine(crc(A), crc(B), len(B))
    static std::uint32_t combine(std::uint32_t crc1, std::uint32_t crc2, std::size_t len2);
    
    // Verify implementation against known check vectors
    static bool verifyCheckVectors();

private:
    static const std::uint32_t TABLE[256];
    
    // CRC32C polynomial: iSCSI polynomial (Castagnoli)
    // x^32 + x^28 + x^27 + x^26 + x^25 + x^23 + x^22 + x^20 + x^19 + x^18 + x^14 + x^13 + x^11 + x^10 + x^9 + x^8 + x^6 + 1
    static constexpr std::uint32_t POLY = 0x82F63B78; // Bit-reflected form
};

} // namespace lockstep
