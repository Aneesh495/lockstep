#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include <cstring>

namespace lockstep {

// Stable hashing for deterministic state digests
// Uses FNV-1a variant for consistent results across runs and platforms
// Never depends on addresses, padding bytes, unordered iteration, or wall-clock data

class StableDigest {
public:
    // FNV-1a constants for 64-bit
    static constexpr std::uint64_t FNV_OFFSET = 0x14650FB0739D0383ULL;
    static constexpr std::uint64_t FNV_PRIME = 0x93D7654B7F1E3E17ULL;
    
    StableDigest() : hash_(FNV_OFFSET) {}
    
    // Hash a single byte
    void update(std::uint8_t byte) {
        hash_ ^= byte;
        hash_ *= FNV_PRIME;
    }
    
    // Hash a range of bytes
    void update(std::span<const std::uint8_t> data) {
        for (std::uint8_t b : data) {
            update(b);
        }
    }
    
    // Hash integer types (little-endian for consistency)
    void update(std::uint16_t value) {
        update(static_cast<std::uint8_t>(value & 0xFF));
        update(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }
    
    void update(std::uint32_t value) {
        update(static_cast<std::uint8_t>(value & 0xFF));
        update(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    }
    
    void update(std::uint64_t value) {
        update(static_cast<std::uint8_t>(value & 0xFF));
        update(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 24) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 32) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 40) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 48) & 0xFF));
        update(static_cast<std::uint8_t>((value >> 56) & 0xFF));
    }
    
    void update(std::int16_t value) {
        update(static_cast<std::uint16_t>(value));
    }
    
    void update(std::int32_t value) {
        update(static_cast<std::uint32_t>(value));
    }
    
    void update(std::int64_t value) {
        update(static_cast<std::uint64_t>(value));
    }
    
    // Get final hash
    std::uint64_t finalize() const {
        return hash_;
    }
    
    // Reset to initial state
    void reset() {
        hash_ = FNV_OFFSET;
    }
    
    // Static convenience function
    static std::uint64_t hash(std::span<const std::uint8_t> data) {
        StableDigest digest;
        digest.update(data);
        return digest.finalize();
    }
    
    // Combine two hashes
    static std::uint64_t combine(std::uint64_t a, std::uint64_t b) {
        StableDigest digest;
        digest.update(a);
        digest.update(b);
        return digest.finalize();
    }

private:
    std::uint64_t hash_;
};

// SHA-256 for manifest and evidence integrity
// Simplified implementation for portability
class Sha256 {
public:
    static constexpr std::size_t DIGEST_SIZE = 32;
    using Digest = std::array<std::uint8_t, DIGEST_SIZE>;
    
    Sha256();
    
    void update(std::span<const std::uint8_t> data);
    void update(const void* data, std::size_t length);
    Digest finalize();
    
    // Static convenience function
    static Digest compute(std::span<const std::uint8_t> data);
    static Digest compute(const void* data, std::size_t length);
    
    // Convert digest to hex string
    static std::array<char, 65> toHex(const Digest& digest);

private:
    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_;
    std::size_t bufferPos_;
    std::uint64_t totalLength_;
    
    void processBlock(const std::uint8_t* block);
    void padAndProcess();
};

} // namespace lockstep
