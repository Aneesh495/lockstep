#include "lockstep/common/stable_digest.hpp"
#include <algorithm>

namespace lockstep {

// SHA-256 implementation
namespace {
    constexpr std::uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    
    inline std::uint32_t rotr(std::uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }
    
    inline std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        return (x & y) ^ (~x & z);
    }
    
    inline std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }
    
    inline std::uint32_t sigma0(std::uint32_t x) {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }
    
    inline std::uint32_t sigma1(std::uint32_t x) {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }
    
    inline std::uint32_t gamma0(std::uint32_t x) {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }
    
    inline std::uint32_t gamma1(std::uint32_t x) {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }
}

Sha256::Sha256() 
    : state_{
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    }
    , buffer_{}
    , bufferPos_(0)
    , totalLength_(0)
{}

void Sha256::update(std::span<const std::uint8_t> data) {
    update(data.data(), data.size());
}

void Sha256::update(const void* data, std::size_t length) {
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    totalLength_ += length;
    
    // Process any buffered data first
    while (bufferPos_ > 0 && length > 0) {
        buffer_[bufferPos_++] = *bytes++;
        --length;
        
        if (bufferPos_ == 64) {
            processBlock(buffer_.data());
            bufferPos_ = 0;
        }
    }
    
    // Process full blocks
    while (length >= 64) {
        processBlock(bytes);
        bytes += 64;
        length -= 64;
    }
    
    // Buffer remaining data
    if (length > 0) {
        std::memcpy(buffer_.data(), bytes, length);
        bufferPos_ = length;
    }
}

Sha256::Digest Sha256::finalize() {
    padAndProcess();
    
    Digest result;
    for (std::size_t i = 0; i < 8; ++i) {
        result[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFF);
        result[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFF);
        result[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFF);
        result[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFF);
    }
    
    return result;
}

Sha256::Digest Sha256::compute(std::span<const std::uint8_t> data) {
    return compute(data.data(), data.size());
}

Sha256::Digest Sha256::compute(const void* data, std::size_t length) {
    Sha256 sha;
    sha.update(data, length);
    return sha.finalize();
}

std::array<char, 65> Sha256::toHex(const Digest& digest) {
    std::array<char, 65> result{};
    static const char hex[] = "0123456789abcdef";
    
    for (std::size_t i = 0; i < DIGEST_SIZE; ++i) {
        result[i * 2] = hex[(digest[i] >> 4) & 0xF];
        result[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    result[64] = '\0';
    
    return result;
}

void Sha256::processBlock(const std::uint8_t* block) {
    std::uint32_t W[64];
    
    // Prepare message schedule
    for (int i = 0; i < 16; ++i) {
        W[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    
    for (int i = 16; i < 64; ++i) {
        W[i] = gamma1(W[i - 2]) + W[i - 7] + gamma0(W[i - 15]) + W[i - 16];
    }
    
    // Initialize working variables
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    
    // Main loop
    for (int i = 0; i < 64; ++i) {
        std::uint32_t T1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
        std::uint32_t T2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }
    
    // Add compressed chunk to current hash value
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::padAndProcess() {
    // Pad message with 1 bit, zeros, and length
    std::uint64_t bitLength = totalLength_ * 8;
    
    // Append 0x80 byte
    buffer_[bufferPos_++] = 0x80;
    
    // If not enough room for length, pad and process
    if (bufferPos_ > 56) {
        while (bufferPos_ < 64) {
            buffer_[bufferPos_++] = 0;
        }
        processBlock(buffer_.data());
        bufferPos_ = 0;
    }
    
    // Pad to 56 bytes
    while (bufferPos_ < 56) {
        buffer_[bufferPos_++] = 0;
    }
    
    // Append length in bits as 64-bit big-endian
    buffer_[56] = static_cast<std::uint8_t>((bitLength >> 56) & 0xFF);
    buffer_[57] = static_cast<std::uint8_t>((bitLength >> 48) & 0xFF);
    buffer_[58] = static_cast<std::uint8_t>((bitLength >> 40) & 0xFF);
    buffer_[59] = static_cast<std::uint8_t>((bitLength >> 32) & 0xFF);
    buffer_[60] = static_cast<std::uint8_t>((bitLength >> 24) & 0xFF);
    buffer_[61] = static_cast<std::uint8_t>((bitLength >> 16) & 0xFF);
    buffer_[62] = static_cast<std::uint8_t>((bitLength >> 8) & 0xFF);
    buffer_[63] = static_cast<std::uint8_t>(bitLength & 0xFF);
    
    processBlock(buffer_.data());
}

} // namespace lockstep
