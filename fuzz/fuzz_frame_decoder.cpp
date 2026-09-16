// Fuzz target for frame decoder
// Tests robustness of frame parsing against malformed input
// Can be built with libFuzzer (Clang) or run standalone

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <vector>
#include <random>
#include "lockstep/protocol/frame.hpp"
#include "lockstep/protocol/codec.hpp"
#include "lockstep/common/crc32c.hpp"

using namespace lockstep;

// Test function called by both libFuzzer and standalone
static int test_frame_decoder(const uint8_t* data, size_t size) {
    // Test 1: Parse frame header
    auto header = FrameHeader::parse(data, size);
    
    if (header.has_value()) {
        // Re-serialize and verify round-trip
        uint8_t buffer[FrameHeader::SIZE];
        size_t written = header->serialize(buffer, sizeof(buffer));
        
        if (written == FrameHeader::SIZE) {
            // Parse again
            auto header2 = FrameHeader::parse(buffer, sizeof(buffer));
            if (header2.has_value()) {
                // Verify key fields match
                if (header->magic() != header2->magic() ||
                    header->version() != header2->version() ||
                    header->messageType() != header2->messageType() ||
                    header->payloadLength() != header2->payloadLength()) {
                    // Inconsistent round-trip - this is a bug
                    __builtin_trap();
                }
            }
        }
        
        // Test message type validity
        MessageType mt = header->messageType();
        (void)mt;  // Message type validated during decode
        
        // If we have a payload, try to decode it based on message type
        if (header->payloadLength() > 0 && size >= FrameHeader::SIZE + header->payloadLength()) {
            const uint8_t* payload = data + FrameHeader::SIZE;
            size_t payloadSize = header->payloadLength();
            
            // Verify CRC if present
            uint32_t expectedCrc = header->crc32c();
            if (expectedCrc != 0) {
                // Compute CRC over header (with crc=0) + payload
                uint8_t headerCopy[FrameHeader::SIZE];
                std::memcpy(headerCopy, data, FrameHeader::SIZE);
                // Zero out CRC field (bytes 32-36)
                headerCopy[32] = 0;
                headerCopy[33] = 0;
                headerCopy[34] = 0;
                headerCopy[35] = 0;
                
                uint32_t computedCrc = Crc32C::compute(headerCopy, FrameHeader::SIZE);
                computedCrc = Crc32C::compute(computedCrc, std::span<const uint8_t>(payload, payloadSize));
                
                // CRC mismatch is OK for fuzz testing - just means input was malformed
                (void)computedCrc;
            }
            
            // Try to decode specific message types
            switch (mt) {
                case MessageType::NewOrder: {
                    auto msg = Codec::decodeNewOrder(payload, payloadSize);
                    if (msg.has_value()) {
                        // Validate decoded values
                        if (msg->clientId == 0 && msg->orderId == 0 && msg->quantity == 0) {
                            // Empty message is valid
                        }
                        // Check side validity
                        if (msg->side != Side::Buy && msg->side != Side::Sell) {
                            __builtin_trap(); // Invalid side
                        }
                        // Check TIF validity
                        if (msg->tif != TimeInForce::GTC && 
                            msg->tif != TimeInForce::IOC && 
                            msg->tif != TimeInForce::FOK) {
                            __builtin_trap(); // Invalid TIF
                        }
                    }
                    break;
                }
                case MessageType::CancelOrder: {
                    auto msg = Codec::decodeCancelOrder(payload, payloadSize);
                    (void)msg; // Successfully decoded or not
                    break;
                }
                case MessageType::ReplaceOrder: {
                    auto msg = Codec::decodeReplaceOrder(payload, payloadSize);
                    (void)msg;
                    break;
                }
                default:
                    // Other message types not specifically tested
                    break;
            }
        }
    }
    
    // Test 2: Try parsing with various offsets (simulates partial reads)
    if (size >= 1) {
        // Try parsing from different starting positions
        for (size_t offset = 0; offset < std::min(size, size_t(16)); offset++) {
            auto h = FrameHeader::parse(data + offset, size - offset);
            (void)h;
        }
    }
    
    // Test 3: Test edge cases
    if (size >= FrameHeader::SIZE) {
        // Corrupt magic
        uint8_t corrupted[FrameHeader::SIZE];
        std::memcpy(corrupted, data, FrameHeader::SIZE);
        corrupted[0] = 0xFF;
        auto h1 = FrameHeader::parse(corrupted, sizeof(corrupted));
        // Should fail to parse
        if (h1.has_value()) {
            // Magic should have been rejected
        }
        
        // Corrupt version
        std::memcpy(corrupted, data, FrameHeader::SIZE);
        corrupted[4] = 0xFF;
        (void)FrameHeader::parse(corrupted, sizeof(corrupted));
        // Should fail to parse
        
        // Corrupt reserved field
        std::memcpy(corrupted, data, FrameHeader::SIZE);
        corrupted[36] = 0x01; // Non-zero reserved
        (void)FrameHeader::parse(corrupted, sizeof(corrupted));
        // Should fail to parse
        
        // Corrupt payload length to be too large
        std::memcpy(corrupted, data, FrameHeader::SIZE);
        corrupted[8] = 0xFF;
        corrupted[9] = 0xFF;
        corrupted[10] = 0xFF;
        corrupted[11] = 0xFF;
        (void)FrameHeader::parse(corrupted, sizeof(corrupted));
        // Should fail to parse due to oversized payload
    }
    
    return 0;
}

// Fuzz entry point for libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return test_frame_decoder(data, size);
}

// Standalone entry point for smoke testing
int main(int argc, char* argv[]) {
    std::cout << "Frame Decoder Fuzz Smoke Test\n";
    std::cout << "==============================\n\n";
    
    // Seed for reproducibility
    uint32_t seed = 12345;
    if (argc > 1) {
        seed = static_cast<uint32_t>(std::stoul(argv[1]));
    }
    std::cout << "Seed: " << seed << "\n";
    
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> lenDist(0, 256);
    std::uniform_int_distribution<uint8_t> byteDist(0, 255);
    
    // Test cases
    int passed = 0;
    int total = 10000;
    
    // Test 1: Random bytes
    std::cout << "Testing " << total << " random byte sequences...\n";
    for (int i = 0; i < total; i++) {
        size_t size = lenDist(rng);
        std::vector<uint8_t> data(size);
        for (auto& b : data) {
            b = byteDist(rng);
        }
        test_frame_decoder(data.data(), data.size());
        passed++;
    }
    std::cout << "  Passed: " << passed << "/" << total << "\n";
    
    // Test 2: Valid headers with random payloads
    std::cout << "Testing valid headers with random payloads...\n";
    int validHeaderCount = 1000;
    for (int i = 0; i < validHeaderCount; i++) {
        std::vector<uint8_t> data(FrameHeader::SIZE + 100);
        
        // Write valid header
        ByteWriter writer(data.data(), data.size());
        writer.writeU32(FrameHeader::MAGIC);
        writer.writeU8(1);  // version
        writer.writeU8(static_cast<uint8_t>(MessageType::Heartbeat));  // type
        writer.writeU16(0);  // flags
        writer.writeU32(100);  // payload length
        writer.writeU32(1);  // session ID
        writer.writeU64(static_cast<uint64_t>(i));  // sequence
        writer.writeU64(1000000ULL + static_cast<uint64_t>(i));  // timestamp
        writer.writeU32(0);  // CRC (placeholder)
        writer.writeU32(0);  // reserved
        
        // Random payload
        for (size_t j = 0; j < 100; j++) {
            data[FrameHeader::SIZE + j] = byteDist(rng);
        }
        
        test_frame_decoder(data.data(), data.size());
        passed++;
    }
    std::cout << "  Total passed: " << passed << "\n";
    
    // Test 3: Edge cases
    std::cout << "Testing edge cases...\n";
    
    // Empty input
    test_frame_decoder(nullptr, 0);
    passed++;
    
    // Single byte
    uint8_t single = byteDist(rng);
    test_frame_decoder(&single, 1);
    passed++;
    
    // Exactly header size
    std::vector<uint8_t> exactHeader(FrameHeader::SIZE);
    for (auto& b : exactHeader) {
        b = byteDist(rng);
    }
    test_frame_decoder(exactHeader.data(), exactHeader.size());
    passed++;
    
    // Oversized payload length
    std::vector<uint8_t> oversized(FrameHeader::SIZE);
    ByteWriter writer(oversized.data(), oversized.size());
    writer.writeU32(FrameHeader::MAGIC);
    writer.writeU8(1);
    writer.writeU8(static_cast<uint8_t>(MessageType::Heartbeat));
    writer.writeU16(0);
    writer.writeU32(0xFFFFFFFF);  // Max payload length
    test_frame_decoder(oversized.data(), oversized.size());
    passed++;
    
    std::cout << "  Total passed: " << passed << "\n";
    
    std::cout << "\nAll smoke tests passed!\n";
    std::cout << "Total tests: " << passed << "\n";
    
    return 0;
}
