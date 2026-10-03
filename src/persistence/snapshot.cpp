#include "lockstep/persistence/snapshot.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include "lockstep/common/crc32c.hpp"
#include "lockstep/common/endian.hpp"

namespace lockstep {

namespace {

constexpr std::uint32_t SNAP_MAGIC = 0x534E4150;  // "SNAP"
constexpr std::uint8_t SNAP_VERSION = 1;
constexpr std::uint32_t MAX_SNAPSHOT_ELEMENTS = 1000000;

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

SnapshotWriter::SnapshotWriter(const std::string& path) : path_(path) {}

bool SnapshotWriter::write(const MatchingEngine& engine, const RiskEngine& risk) {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(65536);

    SnapshotHeader header;
    header.magic = SNAP_MAGIC;
    header.version = SNAP_VERSION;
    header.timestamp = engine.clock().now();
    header.commandSeq = engine.currentCommandSeq();
    header.eventSeq = engine.currentEventSeq();
    header.coveredWalSeq = (engine.currentCommandSeq() > 1) ? (engine.currentCommandSeq() - 1) : 0;

    auto instrumentConfigs = engine.instrumentConfigs();
    header.instrumentCount = static_cast<std::uint32_t>(instrumentConfigs.size());
    header.orderCount = engine.totalOrderCount();
    header.clientCount = static_cast<std::uint32_t>(risk.clients().size());

    // 1. Serialize Header (56 bytes)
    std::size_t headerOffset = buffer.size();
    buffer.resize(headerOffset + sizeof(SnapshotHeader));
    std::memcpy(buffer.data() + headerOffset, &header, sizeof(SnapshotHeader));

    // 2. Serialize Instruments
    for (const auto& instr : instrumentConfigs) {
        std::size_t offset = buffer.size();
        buffer.resize(offset + 4 + 8 + 8 + 8 + 4 + 4 + 8);
        ByteWriter writer(buffer.data() + offset, buffer.size() - offset);
        writer.writeU32(instr.id);
        writer.writeI64(instr.minPrice);
        writer.writeI64(instr.maxPrice);
        writer.writeI64(instr.tickSize);
        writer.writeU32(instr.maxOrdersPerLevel);
        writer.writeU32(instr.maxPriceLevels);
        const OrderBook* book = engine.getBook(instr.id);
        std::uint64_t nextMatchId = (book != nullptr) ? book->nextMatchId() : 1;
        writer.writeU64(nextMatchId);
    }

    // 3. Serialize Resting Orders in exact price-time priority
    for (const auto& instr : instrumentConfigs) {
        const OrderBook* book = engine.getBook(instr.id);
        if (book == nullptr)
            continue;
        book->forEachOrderInPriceTimeOrder([&](const Order& order) {
            std::size_t offset = buffer.size();
            buffer.resize(offset + 4 + 4 + 8 + 1 + 1 + 1 + 5 + 8 + 4 + 4 + 8 + 8 + 8 + 8);
            ByteWriter writer(buffer.data() + offset, buffer.size() - offset);
            writer.writeU32(order.instrumentId);
            writer.writeU32(order.clientId);
            writer.writeU64(order.orderId);
            writer.writeU8(static_cast<std::uint8_t>(order.side));
            writer.writeU8(static_cast<std::uint8_t>(order.tif));
            writer.writeU8(static_cast<std::uint8_t>(order.status));
            writer.writeBytes(reinterpret_cast<const std::uint8_t*>("\0\0\0\0\0"), 5);
            writer.writeI64(order.price);
            writer.writeU32(order.quantity);
            writer.writeU32(order.executedQuantity);
            writer.writeU64(order.clientSeq);
            writer.writeU64(order.engineSeq);
            writer.writeU64(order.createTime);
            writer.writeU64(order.updateTime);
        });
    }

    // 4. Serialize Risk State
    for (const auto& [clientId, client] : risk.clients()) {
        std::size_t offset = buffer.size();
        buffer.resize(offset + 4 + 4 + 4 + 4 + 8 + 8 + 8 + 8 + 4 + 4 + 4 + 8 + 8 + 8 + 1 + 7 + 4);
        ByteWriter writer(buffer.data() + offset, buffer.size() - offset);
        writer.writeU32(client.clientId);
        writer.writeU32(client.openOrderCount);
        writer.writeU32(client.openBuyQuantity);
        writer.writeU32(client.openSellQuantity);
        writer.writeI64(static_cast<std::int64_t>(client.openBuyNotional));
        writer.writeI64(static_cast<std::int64_t>(client.openSellNotional));
        writer.writeI64(static_cast<std::int64_t>(client.reservedBuyNotional));
        writer.writeI64(static_cast<std::int64_t>(client.reservedSellNotional));

        writer.writeU32(client.limits.maxOrderQuantity);
        writer.writeU32(client.limits.maxOpenOrders);
        writer.writeU32(client.limits.maxOpenQuantity);
        writer.writeI64(static_cast<std::int64_t>(client.limits.maxOrderNotional));
        writer.writeI64(static_cast<std::int64_t>(client.limits.maxOpenNotional));
        writer.writeI64(client.limits.maxPosition);
        writer.writeU8(client.limits.killSwitchActive ? 1 : 0);
        writer.writeBytes(reinterpret_cast<const std::uint8_t*>("\0\0\0\0\0\0\0"), 7);

        std::uint32_t posCount = static_cast<std::uint32_t>(client.positions.size());
        writer.writeU32(posCount);

        for (const auto& [instId, pos] : client.positions) {
            std::size_t pOffset = buffer.size();
            buffer.resize(pOffset + 4 + 8);
            ByteWriter pw(buffer.data() + pOffset, buffer.size() - pOffset);
            pw.writeU32(instId);
            pw.writeI64(pos);
        }
    }

    // 5. Serialize State Digests and Footer
    std::uint64_t engineDigest = engine.computeStateDigest();
    std::uint64_t riskDigest = risk.computeDigest();
    std::size_t footerOffset = buffer.size();
    buffer.resize(footerOffset + 8 + 8 + 4);
    ByteWriter footerWriter(buffer.data() + footerOffset, buffer.size() - footerOffset);
    footerWriter.writeU64(engineDigest);
    footerWriter.writeU64(riskDigest);

    // Compute CRC over everything prior to the 4-byte CRC
    std::uint32_t crc = Crc32C::compute(buffer.data(), buffer.size() - 4);
    footerWriter.writeU32(crc);

    // 6. Atomic durable write: temporary file, fsync, rename, directory sync
    std::string tempPath =
        path_ + ".tmp." + std::to_string(::getpid()) + "." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    int fd = ::open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        error_ = "Cannot open temp file for snapshot: " + tempPath;
        return false;
    }

    std::size_t remaining = buffer.size();
    const std::uint8_t* ptr = buffer.data();
    while (remaining > 0) {
        ssize_t n = ::write(fd, ptr, remaining);
        if (n <= 0) {
            error_ = "Failed to write snapshot bytes";
            ::close(fd);
            ::unlink(tempPath.c_str());
            return false;
        }
        ptr += n;
        remaining -= static_cast<std::size_t>(n);
    }

    if (::fsync(fd) != 0) {
        error_ = "Failed to fsync snapshot temp file";
        ::close(fd);
        ::unlink(tempPath.c_str());
        return false;
    }

    ::close(fd);

    if (std::rename(tempPath.c_str(), path_.c_str()) != 0) {
        error_ = "Failed to atomically rename snapshot file";
        ::unlink(tempPath.c_str());
        return false;
    }

    syncDirectory(path_);
    return true;
}

SnapshotReader::SnapshotReader(const std::string& path) : path_(path) {}

bool SnapshotReader::read(MatchingEngine& engine, RiskEngine& risk) {
    int fd = ::open(path_.c_str(), O_RDONLY);
    if (fd < 0) {
        error_ = "Cannot open snapshot file: " + path_;
        return false;
    }

    off_t fileSize = ::lseek(fd, 0, SEEK_END);
    if (fileSize < static_cast<off_t>(sizeof(SnapshotHeader) + 20)) {
        error_ = "Snapshot file too small";
        ::close(fd);
        return false;
    }
    if (fileSize > 100 * 1024 * 1024) {  // 100MB bound
        error_ = "Snapshot file exceeds maximum allowed size";
        ::close(fd);
        return false;
    }

    ::lseek(fd, 0, SEEK_SET);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(fileSize));
    std::size_t bytesRead = 0;
    while (bytesRead < buffer.size()) {
        ssize_t n = ::read(fd, buffer.data() + bytesRead, buffer.size() - bytesRead);
        if (n <= 0) {
            error_ = "Failed to read snapshot file";
            ::close(fd);
            return false;
        }
        bytesRead += static_cast<std::size_t>(n);
    }
    ::close(fd);

    // 1. Verify CRC32C
    std::uint32_t storedCrc = 0;
    ByteReader crcReader(buffer.data() + buffer.size() - 4, 4);
    crcReader.readU32(storedCrc);

    std::uint32_t computedCrc = Crc32C::compute(buffer.data(), buffer.size() - 4);
    if (computedCrc != storedCrc) {
        error_ = "Snapshot CRC mismatch: computed=" + std::to_string(computedCrc) +
                 " stored=" + std::to_string(storedCrc);
        return false;
    }

    // 2. Parse Header
    std::memcpy(&header_, buffer.data(), sizeof(SnapshotHeader));

    if (header_.magic != SNAP_MAGIC) {
        error_ = "Invalid snapshot magic";
        return false;
    }

    if (header_.version != SNAP_VERSION) {
        error_ = "Unsupported snapshot version";
        return false;
    }

    if (header_.instrumentCount > MAX_SNAPSHOT_ELEMENTS ||
        header_.orderCount > MAX_SNAPSHOT_ELEMENTS || header_.clientCount > MAX_SNAPSHOT_ELEMENTS) {
        error_ = "Snapshot element count exceeds safety limit";
        return false;
    }

    ByteReader reader(buffer.data() + sizeof(SnapshotHeader),
                      buffer.size() - sizeof(SnapshotHeader));

    // 3. Parse and Validate Instruments
    engine.reset();
    engine.setSequences(header_.commandSeq, header_.eventSeq);
    risk.clear();

    for (std::uint32_t i = 0; i < header_.instrumentCount; ++i) {
        InstrumentId id;
        Price minPrice, maxPrice, tickSize;
        std::uint32_t maxOrdersPerLevel, maxPriceLevels;
        std::uint64_t nextMatchId;

        if (!reader.readU32(id) || !reader.readI64(minPrice) || !reader.readI64(maxPrice) ||
            !reader.readI64(tickSize) || !reader.readU32(maxOrdersPerLevel) ||
            !reader.readU32(maxPriceLevels) || !reader.readU64(nextMatchId)) {
            error_ = "Truncated instrument section in snapshot";
            return false;
        }

        const InstrumentConfig* cfg = engine.getInstrumentConfig(id);
        if (cfg == nullptr) {
            error_ = "Snapshot contains unknown instrument: " + std::to_string(id);
            return false;
        }

        if (cfg->minPrice != minPrice || cfg->maxPrice != maxPrice || cfg->tickSize != tickSize) {
            error_ = "Snapshot instrument configuration mismatch for ID " + std::to_string(id);
            return false;
        }

        OrderBook* book = engine.getBook(id);
        if (book != nullptr) {
            book->setNextMatchId(nextMatchId);
        }
    }

    // 4. Parse and Install Orders
    std::unordered_map<ClientId, std::pair<Quantity, Quantity>> clientExpectedQty;
    for (std::uint32_t i = 0; i < header_.orderCount; ++i) {
        Order order;
        std::uint8_t sideByte, tifByte, statusByte;
        if (!reader.readU32(order.instrumentId) || !reader.readU32(order.clientId) ||
            !reader.readU64(order.orderId) || !reader.readU8(sideByte) || !reader.readU8(tifByte) ||
            !reader.readU8(statusByte) || !reader.skip(5) || !reader.readI64(order.price) ||
            !reader.readU32(order.quantity) || !reader.readU32(order.executedQuantity) ||
            !reader.readU64(order.clientSeq) || !reader.readU64(order.engineSeq) ||
            !reader.readU64(order.createTime) || !reader.readU64(order.updateTime)) {
            error_ = "Truncated order section in snapshot";
            return false;
        }

        order.side = static_cast<Side>(sideByte);
        order.tif = static_cast<TimeInForce>(tifByte);
        order.status = static_cast<OrderStatus>(statusByte);

        OrderBook* book = engine.getBook(order.instrumentId);
        if (book == nullptr) {
            error_ = "Snapshot order refers to missing book: " + std::to_string(order.instrumentId);
            return false;
        }

        if (!engine.installOrder(order)) {
            error_ = "Failed to install resting order in book for order " +
                     std::to_string(order.orderId);
            return false;
        }

        if (order.side == Side::Buy) {
            clientExpectedQty[order.clientId].first += order.remainingQuantity();
        } else {
            clientExpectedQty[order.clientId].second += order.remainingQuantity();
        }
    }

    // 5. Parse and Install Risk State
    for (std::uint32_t i = 0; i < header_.clientCount; ++i) {
        ClientState client;
        std::int64_t buyNotional, sellNotional, resBuyNotional, resSellNotional;
        std::int64_t limitBuyNotional, limitSellNotional, maxPos;
        std::uint8_t killSwitch;
        std::uint32_t posCount;

        if (!reader.readU32(client.clientId) || !reader.readU32(client.openOrderCount) ||
            !reader.readU32(client.openBuyQuantity) || !reader.readU32(client.openSellQuantity) ||
            !reader.readI64(buyNotional) || !reader.readI64(sellNotional) ||
            !reader.readI64(resBuyNotional) || !reader.readI64(resSellNotional) ||
            !reader.readU32(client.limits.maxOrderQuantity) ||
            !reader.readU32(client.limits.maxOpenOrders) ||
            !reader.readU32(client.limits.maxOpenQuantity) || !reader.readI64(limitBuyNotional) ||
            !reader.readI64(limitSellNotional) || !reader.readI64(maxPos) ||
            !reader.readU8(killSwitch) || !reader.skip(7) || !reader.readU32(posCount)) {
            error_ = "Truncated risk section in snapshot";
            return false;
        }

        client.openBuyNotional = buyNotional;
        client.openSellNotional = sellNotional;
        client.reservedBuyNotional = resBuyNotional;
        client.reservedSellNotional = resSellNotional;
        client.limits.maxOrderNotional = limitBuyNotional;
        client.limits.maxOpenNotional = limitSellNotional;
        client.limits.maxPosition = maxPos;
        client.limits.killSwitchActive = (killSwitch != 0);

        if (posCount > MAX_SNAPSHOT_ELEMENTS) {
            error_ = "Position count exceeds safety limit in snapshot";
            return false;
        }

        for (std::uint32_t p = 0; p < posCount; ++p) {
            InstrumentId instId;
            Position pos;
            if (!reader.readU32(instId) || !reader.readI64(pos)) {
                error_ = "Truncated position section in snapshot";
                return false;
            }
            client.positions[instId] = pos;
        }

        // Cross-validate resting order quantities match risk state open quantities
        auto it = clientExpectedQty.find(client.clientId);
        Quantity expectedBuy = (it != clientExpectedQty.end()) ? it->second.first : 0;
        Quantity expectedSell = (it != clientExpectedQty.end()) ? it->second.second : 0;
        if (client.openBuyQuantity != expectedBuy || client.openSellQuantity != expectedSell) {
            error_ = "Snapshot internally inconsistent: client open orders mismatch";
            return false;
        }

        risk.installClientState(client);
    }

    // 6. Validate State Digests
    std::uint64_t storedEngineDigest, storedRiskDigest;
    if (!reader.readU64(storedEngineDigest) || !reader.readU64(storedRiskDigest)) {
        error_ = "Truncated digest footer in snapshot";
        return false;
    }

    if (engine.computeStateDigest() != storedEngineDigest) {
        error_ = "Snapshot engine state digest mismatch";
        return false;
    }

    if (risk.computeDigest() != storedRiskDigest) {
        error_ = "Snapshot risk state digest mismatch";
        return false;
    }

    if (!engine.checkInvariants(error_)) {
        return false;
    }

    if (!risk.checkInvariants(error_)) {
        return false;
    }

    engine.setSequences(header_.commandSeq, header_.eventSeq);
    return true;
}

}  // namespace lockstep
