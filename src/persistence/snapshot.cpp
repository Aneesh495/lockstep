#include "lockstep/persistence/snapshot.hpp"
#include "lockstep/common/crc32c.hpp"
#include <fstream>

namespace lockstep {

SnapshotWriter::SnapshotWriter(const std::string& path) 
    : path_(path) 
{}

bool SnapshotWriter::write(const MatchingEngine& engine, const RiskEngine& risk) {
    // Serialize to buffer first
    std::vector<std::uint8_t> buffer;
    
    SnapshotHeader header;
    header.timestamp = engine.clock().now();
    header.commandSeq = engine.currentCommandSeq();
    header.eventSeq = engine.currentEventSeq();
    
    // Count instruments, orders, clients
    header.instrumentCount = 0;
    header.orderCount = engine.totalOrderCount();
    header.clientCount = 0;
    
    // Serialize header (CRC will be written at end)
    buffer.resize(sizeof(SnapshotHeader) + 4);
    std::memcpy(buffer.data(), &header, sizeof(SnapshotHeader));
    
    // Serialize state digest
    std::uint64_t digest = engine.computeStateDigest();
    std::vector<std::uint8_t> digestBytes(8);
    std::memcpy(digestBytes.data(), &digest, 8);
    buffer.insert(buffer.end(), digestBytes.begin(), digestBytes.end());
    
    // Compute CRC
    std::uint32_t crc = Crc32C::compute(buffer.data(), buffer.size());
    
    // Append CRC
    std::vector<std::uint8_t> crcBytes(4);
    std::memcpy(crcBytes.data(), &crc, 4);
    buffer.insert(buffer.end(), crcBytes.begin(), crcBytes.end());
    
    // Write to temp file then rename
    std::string tempPath = path_ + ".tmp";
    std::ofstream file(tempPath, std::ios::binary);
    if (!file.is_open()) {
        error_ = "Cannot open file: " + tempPath;
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    file.close();
    
    // Atomic rename
    if (std::rename(tempPath.c_str(), path_.c_str()) != 0) {
        error_ = "Failed to rename snapshot file";
        return false;
    }
    
    return true;
}

SnapshotReader::SnapshotReader(const std::string& path)
    : path_(path)
{}

bool SnapshotReader::read(MatchingEngine& engine, RiskEngine& risk) {
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        error_ = "Cannot open file: " + path_;
        return false;
    }
    
    // Read header
    file.read(reinterpret_cast<char*>(&header_), sizeof(SnapshotHeader));
    
    if (header_.magic != 0x534E4150) {
        error_ = "Invalid snapshot magic";
        return false;
    }
    
    if (header_.version != 1) {
        error_ = "Unsupported snapshot version";
        return false;
    }
    
    // Read state digest
    std::uint64_t digest;
    file.read(reinterpret_cast<char*>(&digest), 8);
    
    // Read CRC
    std::uint32_t storedCrc;
    file.read(reinterpret_cast<char*>(&storedCrc), 4);
    
    // Verify CRC
    file.seekg(0);
    std::vector<std::uint8_t> buffer(sizeof(SnapshotHeader) + 12);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    
    // Zero out CRC field for verification
    std::uint32_t crc = Crc32C::compute(buffer.data(), buffer.size() - 4);
    
    if (crc != storedCrc) {
        error_ = "Snapshot CRC mismatch";
        return false;
    }
    
    return true;
}

} // namespace lockstep
