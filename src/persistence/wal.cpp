#include "lockstep/persistence/wal.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include "lockstep/common/endian.hpp"
#include "lockstep/protocol/codec.hpp"

namespace lockstep {

namespace {

void syncDirectory(const std::string& path) {
    std::filesystem::path p(path);
    std::string dir = p.parent_path().string();
    if (dir.empty()) {
        dir = ".";
    }
    int dirFd = ::open(dir.c_str(), O_RDONLY);
    if (dirFd >= 0) {
        ::fsync(dirFd);
        ::close(dirFd);
    }
}

}  // namespace

WalWriter::WalWriter(const std::string& path) : path_(path), fd_(-1), position_(0) {
    bool isNew = !std::filesystem::exists(path);
    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ >= 0) {
        off_t end = ::lseek(fd_, 0, SEEK_END);
        if (end >= 0) {
            position_ = static_cast<std::uint64_t>(end);
        }
        if (isNew) {
            syncDirectory(path);
        }
    }
}

WalWriter::~WalWriter() {
    close();
}

bool WalWriter::append(std::uint64_t commandSeq, std::uint64_t timestamp, const std::uint8_t* data,
                       std::size_t length, std::uint8_t recordKind) {
    if (fd_ < 0 || length > WAL_MAX_PAYLOAD) {
        return false;
    }

    std::uint16_t payloadLength = static_cast<std::uint16_t>(length);
    std::vector<std::uint8_t> buffer(WAL_HEADER_SIZE + length + 4);

    ByteWriter writer(buffer.data(), buffer.size());
    writer.writeU32(WAL_MAGIC);
    writer.writeU8(WAL_VERSION);
    writer.writeU8(recordKind);
    writer.writeU16(payloadLength);
    writer.writeU64(commandSeq);
    writer.writeU64(timestamp);
    if (length > 0 && data != nullptr) {
        writer.writeBytes(data, length);
    }

    std::uint32_t crc = Crc32C::compute(buffer.data(), WAL_HEADER_SIZE + length);
    writer.writeU32(crc);

    if (interruptionHook_) {
        const auto cut = WAL_HEADER_SIZE + length / 2;
        if (::write(fd_, buffer.data(), cut) != static_cast<ssize_t>(cut))
            return false;
        interruptionHook_();
        // A returning hook is unsupported; this checkpoint is for process interruption.
        return false;
    }
    std::size_t totalBytes = buffer.size();
    const std::uint8_t* ptr = buffer.data();
    while (totalBytes > 0) {
        ssize_t written = ::write(fd_, ptr, totalBytes);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            return false;
        }
        ptr += written;
        totalBytes -= static_cast<std::size_t>(written);
    }

    position_ += buffer.size();
    return true;
}

bool WalWriter::appendNewOrder(std::uint64_t commandSeq, std::uint64_t timestamp,
                               const NewOrderPayload& payload) {
    std::vector<std::uint8_t> buf(sizeof(NewOrderPayload));
    std::size_t sz = Codec::encodeNewOrder(payload, buf.data(), buf.size());
    if (sz == 0)
        return false;
    return append(commandSeq, timestamp, buf.data(), sz,
                  static_cast<std::uint8_t>(MessageType::NewOrder));
}

bool WalWriter::appendCancelOrder(std::uint64_t commandSeq, std::uint64_t timestamp,
                                  const CancelOrderPayload& payload) {
    std::vector<std::uint8_t> buf(sizeof(CancelOrderPayload));
    std::size_t sz = Codec::encodeCancelOrder(payload, buf.data(), buf.size());
    if (sz == 0)
        return false;
    return append(commandSeq, timestamp, buf.data(), sz,
                  static_cast<std::uint8_t>(MessageType::CancelOrder));
}

bool WalWriter::appendReplaceOrder(std::uint64_t commandSeq, std::uint64_t timestamp,
                                   const ReplaceOrderPayload& payload) {
    std::vector<std::uint8_t> buf(sizeof(ReplaceOrderPayload));
    std::size_t sz = Codec::encodeReplaceOrder(payload, buf.data(), buf.size());
    if (sz == 0)
        return false;
    return append(commandSeq, timestamp, buf.data(), sz,
                  static_cast<std::uint8_t>(MessageType::ReplaceOrder));
}

bool WalWriter::appendMassCancel(std::uint64_t commandSeq, std::uint64_t timestamp,
                                 const MassCancelPayload& payload) {
    std::vector<std::uint8_t> buf(sizeof(MassCancelPayload));
    std::size_t sz = Codec::encodeMassCancel(payload, buf.data(), buf.size());
    if (sz == 0)
        return false;
    return append(commandSeq, timestamp, buf.data(), sz,
                  static_cast<std::uint8_t>(MessageType::MassCancel));
}

bool WalWriter::sync() {
    if (fd_ < 0)
        return false;
    return ::fsync(fd_) == 0;
}

void WalWriter::close() {
    if (fd_ >= 0) {
        ::fsync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

WalReader::WalReader(const std::string& path)
    : path_(path), fd_(-1), position_(0), size_(0), lastStatus_(WalStatus::Ok) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ >= 0) {
        off_t end = ::lseek(fd_, 0, SEEK_END);
        if (end >= 0) {
            size_ = static_cast<std::uint64_t>(end);
        }
        ::lseek(fd_, 0, SEEK_SET);
    } else {
        lastStatus_ = WalStatus::ReadError;
        error_ = "Failed to open WAL file: " + path;
    }
}

WalReader::~WalReader() {
    close();
}

WalStatus WalReader::readRecord(WalRecord& record) {
    if (fd_ < 0) {
        lastStatus_ = WalStatus::ReadError;
        error_ = "WAL file not open";
        return lastStatus_;
    }

    // Refresh size in case file was appended to
    off_t currEnd = ::lseek(fd_, 0, SEEK_END);
    if (currEnd >= 0) {
        size_ = static_cast<std::uint64_t>(currEnd);
    }
    ::lseek(fd_, static_cast<off_t>(position_), SEEK_SET);

    if (position_ >= size_) {
        lastStatus_ = WalStatus::CleanEof;
        return lastStatus_;
    }

    std::uint64_t remainingBytes = size_ - position_;
    if (remainingBytes < WAL_HEADER_SIZE) {
        lastStatus_ = WalStatus::IncompleteTail;
        error_ = "Incomplete header at end of WAL: " + std::to_string(remainingBytes) + " bytes";
        return lastStatus_;
    }

    std::uint8_t headerBuf[WAL_HEADER_SIZE];
    ssize_t n = ::read(fd_, headerBuf, WAL_HEADER_SIZE);
    if (n != static_cast<ssize_t>(WAL_HEADER_SIZE)) {
        lastStatus_ = WalStatus::ReadError;
        error_ = "Failed to read WAL header";
        return lastStatus_;
    }

    ByteReader hr(headerBuf, WAL_HEADER_SIZE);
    std::uint32_t magic = 0;
    std::uint8_t version = 0;
    std::uint8_t recordKind = 0;
    std::uint16_t payloadLength = 0;
    std::uint64_t commandSeq = 0;
    std::uint64_t timestamp = 0;

    hr.readU32(magic);
    hr.readU8(version);
    hr.readU8(recordKind);
    hr.readU16(payloadLength);
    hr.readU64(commandSeq);
    hr.readU64(timestamp);

    if (magic != WAL_MAGIC) {
        lastStatus_ = WalStatus::InvalidMagic;
        error_ = "Invalid WAL magic: 0x" + std::to_string(magic);
        return lastStatus_;
    }

    if (version != WAL_VERSION) {
        lastStatus_ = WalStatus::UnsupportedVersion;
        error_ = "Unsupported WAL version: " + std::to_string(version);
        return lastStatus_;
    }

    if (payloadLength > WAL_MAX_PAYLOAD) {
        lastStatus_ = WalStatus::InvalidLength;
        error_ = "Invalid WAL record payload length: " + std::to_string(payloadLength);
        return lastStatus_;
    }

    std::size_t expectedLength = 0;
    switch (static_cast<MessageType>(recordKind)) {
        case MessageType::NewOrder:
            expectedLength = sizeof(NewOrderPayload);
            break;
        case MessageType::CancelOrder:
            expectedLength = sizeof(CancelOrderPayload);
            break;
        case MessageType::ReplaceOrder:
            expectedLength = sizeof(ReplaceOrderPayload);
            break;
        case MessageType::MassCancel:
            expectedLength = sizeof(MassCancelPayload);
            break;
        default:
            lastStatus_ = WalStatus::InvalidLength;
            error_ = "Unsupported WAL record kind";
            return lastStatus_;
    }
    if (payloadLength != expectedLength && payloadLength != expectedLength + FRAME_HEADER_SIZE) {
        lastStatus_ = WalStatus::InvalidLength;
        error_ = "Invalid WAL command length";
        return lastStatus_;
    }

    std::size_t neededRemaining = static_cast<std::size_t>(payloadLength) + 4;
    if (remainingBytes - WAL_HEADER_SIZE < neededRemaining) {
        lastStatus_ = WalStatus::IncompleteTail;
        error_ = "Incomplete payload/CRC at end of WAL";
        return lastStatus_;
    }

    std::vector<std::uint8_t> payload(payloadLength);
    if (payloadLength > 0) {
        ssize_t pn = ::read(fd_, payload.data(), payloadLength);
        if (pn != static_cast<ssize_t>(payloadLength)) {
            lastStatus_ = WalStatus::ReadError;
            error_ = "Failed to read WAL payload";
            return lastStatus_;
        }
    }

    std::uint8_t crcBuf[4];
    ssize_t cn = ::read(fd_, crcBuf, 4);
    if (cn != 4) {
        lastStatus_ = WalStatus::ReadError;
        error_ = "Failed to read WAL CRC";
        return lastStatus_;
    }

    ByteReader cr(crcBuf, 4);
    std::uint32_t storedCrc = 0;
    cr.readU32(storedCrc);

    // Verify CRC over header + payload
    std::vector<std::uint8_t> checkBuf(WAL_HEADER_SIZE + payloadLength);
    std::memcpy(checkBuf.data(), headerBuf, WAL_HEADER_SIZE);
    if (payloadLength > 0) {
        std::memcpy(checkBuf.data() + WAL_HEADER_SIZE, payload.data(), payloadLength);
    }
    std::uint32_t computedCrc = Crc32C::compute(checkBuf.data(), checkBuf.size());

    if (computedCrc != storedCrc) {
        lastStatus_ = WalStatus::CorruptChecksum;
        error_ = "WAL record CRC mismatch: computed=" + std::to_string(computedCrc) +
                 " stored=" + std::to_string(storedCrc);
        return lastStatus_;
    }

    record.magic = magic;
    record.version = version;
    record.recordKind = recordKind;
    record.payloadLength = payloadLength;
    record.commandSeq = commandSeq;
    record.timestamp = timestamp;
    record.payload = std::move(payload);
    record.crc32c = storedCrc;

    position_ += WAL_HEADER_SIZE + payloadLength + 4;
    lastStatus_ = WalStatus::Ok;
    return lastStatus_;
}

std::optional<WalRecord> WalReader::readNext() {
    WalRecord rec;
    WalStatus s = readRecord(rec);
    if (s == WalStatus::Ok) {
        return rec;
    }
    return std::nullopt;
}

bool WalReader::seek(std::uint64_t pos) {
    if (fd_ < 0)
        return false;
    off_t res = ::lseek(fd_, static_cast<off_t>(pos), SEEK_SET);
    if (res >= 0) {
        position_ = static_cast<std::uint64_t>(res);
        return true;
    }
    return false;
}

void WalReader::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace lockstep
