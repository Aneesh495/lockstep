#pragma once

#include <cstdint>
#include <string>
#include <fstream>
#include <vector>
#include <optional>
#include "lockstep/common/types.hpp"
#include "lockstep/common/crc32c.hpp"

namespace lockstep {

// Write-ahead log for durable persistence
// Canonical append-only format with CRC32C

struct WalRecord {
    std::uint32_t magic = 0x57414C4B; // "WALK"
    std::uint8_t version = 1;
    std::uint8_t recordKind = 0;
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
    
    bool isOpen() const { return file_.is_open(); }
    
    // Append a command to the WAL
    bool append(std::uint64_t commandSeq, std::uint64_t timestamp,
                const std::uint8_t* data, std::size_t length);
    
    // Sync to disk
    bool sync();
    
    // Close the WAL
    void close();
    
    // Get current position
    std::uint64_t position() const { return position_; }

private:
    std::string path_;
    std::ofstream file_;
    int fd_ = -1;  // File descriptor for fsync
    std::uint64_t position_ = 0;
};

class WalReader {
public:
    explicit WalReader(const std::string& path);
    
    bool isOpen() const { return file_.is_open(); }
    
    // Read next record
    std::optional<WalRecord> readNext();
    
    // Seek to position
    bool seek(std::uint64_t position);
    
    // Get current position
    std::uint64_t position() const { return position_; }
    
    // Get file size
    std::uint64_t size() const;
    
    // Close the reader
    void close();

private:
    std::string path_;
    std::ifstream file_;
    std::uint64_t position_ = 0;
    std::uint64_t size_ = 0;
};

} // namespace lockstep
