#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <optional>
#include "lockstep/common/types.hpp"
#include "lockstep/common/endian.hpp"

namespace lockstep {

// Frame header (40 bytes) - all integers in network byte order
// 
// Offset  Size  Field
// 0       4     magic (0x4C4B5354 = "LKST")
// 4       1     version (1)
// 5       1     message_type
// 6       2     flags
// 8       4     payload_length
// 12      4     session_id
// 16      8     sequence
// 24      8     send_timestamp_ns
// 32      4     crc32c (covers header with crc=0 + payload)
// 36      4     reserved (must be zero)

struct alignas(1) FrameHeaderData {
    std::uint8_t magic[4];
    std::uint8_t version;
    std::uint8_t messageType;
    std::uint8_t flags[2];
    std::uint8_t payloadLength[4];
    std::uint8_t sessionId[4];
    std::uint8_t sequence[8];
    std::uint8_t sendTimestampNs[8];
    std::uint8_t crc32c[4];
    std::uint8_t reserved[4];
};

class FrameHeader {
public:
    static constexpr std::size_t SIZE = 40;
    static constexpr std::uint32_t MAGIC = 0x4C4B5354;
    static constexpr std::uint8_t VERSION = 1;
    
    // Parse header from bytes
    static std::optional<FrameHeader> parse(const std::uint8_t* data, std::size_t size) {
        if (size < SIZE) {
            return std::nullopt;
        }
        
        FrameHeader header;
        ByteReader reader(data, size);
        
        std::uint32_t magic;
        if (!reader.readU32(magic) || magic != MAGIC) {
            return std::nullopt;
        }
        
        std::uint8_t version;
        if (!reader.readU8(version) || version != VERSION) {
            return std::nullopt;
        }
        
        std::uint8_t messageType;
        if (!reader.readU8(messageType)) {
            return std::nullopt;
        }
        
        std::uint16_t flags;
        if (!reader.readU16(flags)) {
            return std::nullopt;
        }
        
        std::uint32_t payloadLength;
        if (!reader.readU32(payloadLength) || payloadLength > MAX_PAYLOAD_SIZE) {
            return std::nullopt;
        }
        
        std::uint32_t sessionId;
        if (!reader.readU32(sessionId)) {
            return std::nullopt;
        }
        
        std::uint64_t sequence;
        if (!reader.readU64(sequence)) {
            return std::nullopt;
        }
        
        std::uint64_t sendTimestampNs;
        if (!reader.readU64(sendTimestampNs)) {
            return std::nullopt;
        }
        
        std::uint32_t crc32c;
        if (!reader.readU32(crc32c)) {
            return std::nullopt;
        }
        
        std::uint32_t reserved;
        if (!reader.readU32(reserved) || reserved != 0) {
            return std::nullopt;
        }
        
        header.magic_ = magic;
        header.version_ = version;
        header.messageType_ = static_cast<MessageType>(messageType);
        header.flags_ = flags;
        header.payloadLength_ = payloadLength;
        header.sessionId_ = sessionId;
        header.sequence_ = sequence;
        header.sendTimestampNs_ = sendTimestampNs;
        header.crc32c_ = crc32c;
        
        return header;
    }
    
    // Serialize header to bytes
    std::size_t serialize(std::uint8_t* data, std::size_t size) const {
        if (size < SIZE) {
            return 0;
        }
        
        ByteWriter writer(data, size);
        
        writer.writeU32(magic_);
        writer.writeU8(version_);
        writer.writeU8(static_cast<std::uint8_t>(messageType_));
        writer.writeU16(flags_);
        writer.writeU32(payloadLength_);
        writer.writeU32(sessionId_);
        writer.writeU64(sequence_);
        writer.writeU64(sendTimestampNs_);
        writer.writeU32(crc32c_);
        writer.writeU32(0); // reserved
        
        return writer.pos();
    }
    
    // Getters
    std::uint32_t magic() const { return magic_; }
    std::uint8_t version() const { return version_; }
    MessageType messageType() const { return messageType_; }
    std::uint16_t flags() const { return flags_; }
    std::uint32_t payloadLength() const { return payloadLength_; }
    std::uint32_t sessionId() const { return sessionId_; }
    std::uint64_t sequence() const { return sequence_; }
    std::uint64_t sendTimestampNs() const { return sendTimestampNs_; }
    std::uint32_t crc32c() const { return crc32c_; }
    
    // Setters
    void setMessageType(MessageType type) { messageType_ = type; }
    void setFlags(std::uint16_t flags) { flags_ = flags; }
    void setPayloadLength(std::uint32_t len) { payloadLength_ = len; }
    void setSessionId(std::uint32_t id) { sessionId_ = id; }
    void setSequence(std::uint64_t seq) { sequence_ = seq; }
    void setSendTimestampNs(std::uint64_t ts) { sendTimestampNs_ = ts; }
    void setCrc32c(std::uint32_t crc) { crc32c_ = crc; }
    
    // Total frame size
    std::size_t totalSize() const { return SIZE + payloadLength_; }

private:
    std::uint32_t magic_ = MAGIC;
    std::uint8_t version_ = VERSION;
    MessageType messageType_ = MessageType::Heartbeat;
    std::uint16_t flags_ = 0;
    std::uint32_t payloadLength_ = 0;
    std::uint32_t sessionId_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint64_t sendTimestampNs_ = 0;
    std::uint32_t crc32c_ = 0;
};

} // namespace lockstep
