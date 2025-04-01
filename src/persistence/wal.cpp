#include "lockstep/persistence/wal.hpp"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

namespace lockstep {

WalWriter::WalWriter(const std::string& path) 
    : path_(path)
    , file_(path, std::ios::binary | std::ios::app)
    , fd_(-1)
{
    // Open file descriptor for fsync
    fd_ = ::open(path.c_str(), O_WRONLY | O_APPEND, 0644);
}

WalWriter::~WalWriter() {
    close();
}

bool WalWriter::append(std::uint64_t commandSeq, std::uint64_t timestamp,
                       const std::uint8_t* data, std::size_t length) {
    if (!file_.is_open()) return false;
    
    WalRecord record;
    record.magic = 0x57414C4B; // "WALK"
    record.version = 1;
    record.recordKind = 1;
    record.payloadLength = static_cast<std::uint16_t>(length);
    record.commandSeq = commandSeq;
    record.timestamp = timestamp;
    record.payload.assign(data, data + length);
    
    // Compute CRC
    std::vector<std::uint8_t> buffer;
    buffer.resize(24 + length); // header size
    
    std::uint32_t pos = 0;
    std::memcpy(buffer.data() + pos, &record.magic, 4); pos += 4;
    std::memcpy(buffer.data() + pos, &record.version, 1); pos += 1;
    std::memcpy(buffer.data() + pos, &record.recordKind, 1); pos += 1;
    std::memcpy(buffer.data() + pos, &record.payloadLength, 2); pos += 2;
    std::memcpy(buffer.data() + pos, &record.commandSeq, 8); pos += 8;
    std::memcpy(buffer.data() + pos, &record.timestamp, 8); pos += 8;
    std::memcpy(buffer.data() + pos, data, length); pos += static_cast<std::uint32_t>(length);
    
    record.crc32c = Crc32C::compute(buffer.data(), buffer.size());
    
    // Write record
    file_.write(reinterpret_cast<const char*>(&record.magic), 4);
    file_.write(reinterpret_cast<const char*>(&record.version), 1);
    file_.write(reinterpret_cast<const char*>(&record.recordKind), 1);
    file_.write(reinterpret_cast<const char*>(&record.payloadLength), 2);
    file_.write(reinterpret_cast<const char*>(&record.commandSeq), 8);
    file_.write(reinterpret_cast<const char*>(&record.timestamp), 8);
    file_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
    file_.write(reinterpret_cast<const char*>(&record.crc32c), 4);
    
    position_ += 28 + length;
    
    return file_.good();
}

bool WalWriter::sync() {
    if (!file_.is_open() || fd_ < 0) return false;
    
    file_.flush();
    return ::fsync(fd_) == 0;
}

void WalWriter::close() {
    if (file_.is_open()) {
        sync();
        file_.close();
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

WalReader::WalReader(const std::string& path)
    : path_(path)
    , file_(path, std::ios::binary)
{
    if (file_.is_open()) {
        file_.seekg(0, std::ios::end);
        size_ = static_cast<std::uint64_t>(file_.tellg());
        file_.seekg(0, std::ios::beg);
    }
}

std::optional<WalRecord> WalReader::readNext() {
    if (!file_.is_open() || file_.eof()) {
        return std::nullopt;
    }
    
    WalRecord record;
    
    // Read header
    file_.read(reinterpret_cast<char*>(&record.magic), 4);
    if (file_.eof() || file_.gcount() != 4) {
        return std::nullopt;
    }
    
    file_.read(reinterpret_cast<char*>(&record.version), 1);
    file_.read(reinterpret_cast<char*>(&record.recordKind), 1);
    file_.read(reinterpret_cast<char*>(&record.payloadLength), 2);
    file_.read(reinterpret_cast<char*>(&record.commandSeq), 8);
    file_.read(reinterpret_cast<char*>(&record.timestamp), 8);
    
    if (file_.eof() || file_.gcount() != 8) {
        return std::nullopt;
    }
    
    // Read payload
    record.payload.resize(record.payloadLength);
    file_.read(reinterpret_cast<char*>(record.payload.data()), record.payloadLength);
    
    // Read CRC
    file_.read(reinterpret_cast<char*>(&record.crc32c), 4);
    
    if (!file_.good()) {
        return std::nullopt;
    }
    
    return record;
}

bool WalReader::seek(std::uint64_t position) {
    if (!file_.is_open()) return false;
    file_.seekg(static_cast<std::streamoff>(position));
    return file_.good();
}

void WalReader::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

} // namespace lockstep
