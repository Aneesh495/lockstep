#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "lockstep/common/crc32c.hpp"
#include "lockstep/common/types.hpp"
#include "lockstep/protocol/messages.hpp"

namespace lockstep {

// Write-ahead log for durable persistence
// Canonical append-only format with CRC32C

constexpr std::uint32_t WAL_MAGIC = 0x57414C4B;  // "WALK"
constexpr std::uint8_t WAL_VERSION = 1;
constexpr std::size_t WAL_HEADER_SIZE = 24;
constexpr std::uint16_t WAL_MAX_PAYLOAD = 65535;

enum class WalStatus {
    Ok,
    CleanEof,
    IncompleteTail,      // Incomplete record at end of file (recoverable final-tail case)
    CorruptChecksum,     // CRC32C failure
    InvalidMagic,        // Header magic != WALK
    UnsupportedVersion,  // Header version != 1
    InvalidLength,       // Payload length > MAX or exceeds file boundary unexpectedly
    ReadError            // I/O read failure
};

struct WalRecord {
    std::uint32_t magic = WAL_MAGIC;
    std::uint8_t version = WAL_VERSION;
    std::uint8_t recordKind = 1;
    std::uint16_t payloadLength = 0;
    std::uint64_t commandSeq = 0;
    std::uint64_t timestamp = 0;
    std::vector<std::uint8_t> payload;
    std::uint32_t crc32c = 0;
};

class WalWriter {
   public:
    explicit WalWriter(const std::string& path);
    ~WalWriter();

    bool isOpen() const { return fd_ >= 0; }

    // Append a command to the WAL
    bool append(std::uint64_t commandSeq, std::uint64_t timestamp, const std::uint8_t* data,
                std::size_t length, std::uint8_t recordKind = 1);

    // Helpers to append strongly-typed commands
    bool appendNewOrder(std::uint64_t commandSeq, std::uint64_t timestamp,
                        const NewOrderPayload& payload);
    bool appendCancelOrder(std::uint64_t commandSeq, std::uint64_t timestamp,
                           const CancelOrderPayload& payload);
    bool appendReplaceOrder(std::uint64_t commandSeq, std::uint64_t timestamp,
                            const ReplaceOrderPayload& payload);
    bool appendMassCancel(std::uint64_t commandSeq, std::uint64_t timestamp,
                          const MassCancelPayload& payload);

    // Sync to disk
    bool sync();

    // Close the WAL
    void close();

    // Get current position
    std::uint64_t position() const { return position_; }

   private:
    std::string path_;
    int fd_ = -1;
    std::uint64_t position_ = 0;
};

class WalReader {
   public:
    explicit WalReader(const std::string& path);
    ~WalReader();

    bool isOpen() const { return fd_ >= 0; }

    // Read next record with detailed status
    WalStatus readRecord(WalRecord& outRecord);

    // Read next record (returns nullopt on EOF or error, status() has details)
    std::optional<WalRecord> readNext();

    WalStatus status() const { return lastStatus_; }
    const std::string& error() const { return error_; }

    // Seek to position
    bool seek(std::uint64_t position);

    // Get current position
    std::uint64_t position() const { return position_; }

    // Get file size
    std::uint64_t size() const { return size_; }

    // Close the reader
    void close();

   private:
    std::string path_;
    int fd_ = -1;
    std::uint64_t position_ = 0;
    std::uint64_t size_ = 0;
    WalStatus lastStatus_ = WalStatus::Ok;
    std::string error_;
};

}  // namespace lockstep
