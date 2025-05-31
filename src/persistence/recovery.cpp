#include "lockstep/persistence/recovery.hpp"
#include <algorithm>
#include <filesystem>
#include "lockstep/common/crc32c.hpp"
#include "lockstep/protocol/codec.hpp"
#include "lockstep/protocol/frame.hpp"

namespace lockstep {
RecoveryManager::RecoveryManager(const std::string& snapshotPath, const std::string& walPath)
    : snapshotPath_(snapshotPath), walPath_(walPath) {}

bool RecoveryManager::applyRecord(MatchingEngine& engine, RiskEngine& risk, const WalRecord& record,
                                  std::string& error, MatchingEngine::Result* output) {
    const auto* data = record.payload.data();
    auto size = record.payload.size();
    MessageType type = static_cast<MessageType>(record.recordKind);
    if (size >= FrameHeader::SIZE && data[0] == 'L' && data[1] == 'K') {
        auto h = FrameHeader::parse(data, size);
        if (!h || h->totalSize() != size) {
            error = "Invalid persisted frame";
            return false;
        }
        auto bytes = record.payload;
        std::fill(bytes.begin() + 32, bytes.begin() + 36, 0);
        if (Crc32C::compute(bytes.data(), bytes.size()) != h->crc32c()) {
            error = "Persisted frame CRC";
            return false;
        }
        type = h->messageType();
        data += FrameHeader::SIZE;
        size = h->payloadLength();
    }
    if (record.commandSeq != engine.currentCommandSeq()) {
        error = "Command sequence discontinuity";
        return false;
    }
    engine.clock().set(record.timestamp);
    MatchingEngine::Result result;
    auto find = [&](ClientId client, OrderId id) -> std::optional<Order> {
        for (const auto& cfg : engine.config().instruments) {
            auto o = engine.getBook(cfg.id)->findOrder(client, id);
            if (o)
                return o->get();
        }
        return std::nullopt;
    };
    auto initialize = [&](ClientId id) {
        if (!risk.getClientState(id)) {
            RiskLimits lim;
            lim.clientId = id;
            risk.setClientLimits(id, lim);
        }
    };
    Side aggressorSide = Side::Buy;
    InstrumentId instrument = INVALID_INSTRUMENT_ID;
    if (type == MessageType::NewOrder) {
        auto p = Codec::decodeNewOrder(data, size);
        if (!p || size != 56) {
            error = "Invalid NewOrder payload";
            return false;
        }
        Order o;
        o.clientId = p->clientId;
        o.orderId = p->orderId;
        o.instrumentId = p->instrumentId;
        o.side = p->side;
        o.tif = p->tif;
        o.price = p->price;
        o.quantity = p->quantity;
        o.clientSeq = p->clientSeq;
        aggressorSide = o.side;
        instrument = o.instrumentId;
        initialize(o.clientId);
        auto cfg = engine.getInstrumentConfig(instrument);
        if (!cfg)
            result = engine.newOrder(o);
        else {
            auto [ok, reason] =
                risk.checkNewOrder(o.clientId, instrument, o.side, o.price, o.quantity, *cfg);
            if (!ok) {
                result.reason = reason;
                result.commandSeq = engine.nextCommandSeq();
            } else
                result = engine.newOrder(o);
        }
    } else if (type == MessageType::CancelOrder) {
        auto p = Codec::decodeCancelOrder(data, size);
        if (!p || size != 32) {
            error = "Invalid CancelOrder payload";
            return false;
        }
        result = engine.cancelOrder(p->clientId, p->orderId);
    } else if (type == MessageType::ReplaceOrder) {
        auto p = Codec::decodeReplaceOrder(data, size);
        if (!p || size != 48) {
            error = "Invalid ReplaceOrder payload";
            return false;
        }
        auto old = find(p->clientId, p->oldOrderId);
        if (!old) {
            result.commandSeq = engine.nextCommandSeq();
            result.reason = RejectionReason::OrderNotFound;
        } else {
            instrument = old->instrumentId;
            aggressorSide = old->side;
            auto state = *risk.getClientState(p->clientId);
            // Replacement preflight evaluates exposure after releasing the old reservation.
            risk.releaseOrder(old->clientId, instrument, old->side, old->price,
                              old->remainingQuantity());
            auto [ok, reason] =
                risk.checkNewOrder(old->clientId, instrument, old->side, p->newPrice,
                                   p->newQuantity, *engine.getInstrumentConfig(instrument));
            risk.installClientState(state);
            if (!ok) {
                result.commandSeq = engine.nextCommandSeq();
                result.reason = reason;
            } else
                result = engine.replaceOrder(p->clientId, p->oldOrderId, p->newOrderId, instrument,
                                             p->newPrice, p->newQuantity);
        }
    } else if (type == MessageType::MassCancel) {
        auto p = Codec::decodeMassCancel(data, size);
        if (!p || size != 24) {
            error = "Invalid MassCancel payload";
            return false;
        }
        result.commandSeq = engine.currentCommandSeq();
        engine.massCancel(p->clientId);
        result.success = true;
    } else {
        error = "Unsupported WAL command";
        return false;
    }
    // Position updates and open exposure have distinct ownership. Rebuild exposure from
    // resting orders, including partial fills and price-changing replacements.
    for (const auto& m : result.matches) {
        initialize(m.passiveClientId);
        initialize(m.aggressiveClientId);
        risk.updatePosition(m.aggressiveClientId, instrument, aggressorSide, m.quantity);
        risk.updatePosition(m.passiveClientId, instrument,
                            aggressorSide == Side::Buy ? Side::Sell : Side::Buy, m.quantity);
    }
    std::vector<ClientId> clients;
    for (const auto& [id, state] : risk.clients()) {
        (void)state;
        clients.push_back(id);
    }
    for (auto id : clients)
        risk.onMassCancel(id);
    for (const auto& cfg : engine.config().instruments)
        engine.getBook(cfg.id)->forEachOrder([&](const Order& o) {
            initialize(o.clientId);
            risk.reserveOrder(o.clientId, o.instrumentId, o.side, o.price, o.remainingQuantity());
        });
    if (output)
        *output = result;
    return true;
}

bool RecoveryManager::recover(MatchingEngine& destination, RiskEngine& destinationRisk) {
    error_.clear();
    replayedMatches_.clear();
    recoveredCommandSeq_ = 0;
    recoveredEventSeq_ = 0;
    replayedRecords_ = 0;
    finalTailIncomplete_ = false;
    MatchingEngine engine(destination.config());
    RiskEngine risk;
    if (!snapshotPath_.empty()) {
        SnapshotReader reader(snapshotPath_);
        if (!reader.read(engine, risk)) {
            error_ = "Snapshot: " + reader.error();
            return false;
        }
        recoveredCommandSeq_ = reader.coveredWalSeq();
        recoveredEventSeq_ = reader.eventSeq();
    }
    if (!walPath_.empty()) {
        WalReader reader(walPath_);
        if (!reader.isOpen()) {
            error_ = "Cannot open WAL";
            return false;
        }
        const auto covered = recoveredCommandSeq_;
        std::uint64_t previous = 0;
        bool havePrevious = false;
        while (true) {
            WalRecord record;
            const auto status = reader.readRecord(record);
            if (status == WalStatus::CleanEof)
                break;
            if (status == WalStatus::IncompleteTail) {
                finalTailIncomplete_ = true;
                break;
            }
            if (status != WalStatus::Ok) {
                error_ = reader.error();
                return false;
            }
            if ((havePrevious && record.commandSeq != previous + 1) || record.commandSeq == 0) {
                error_ = "Duplicate or Missing WAL sequence gap";
                return false;
            }
            previous = record.commandSeq;
            havePrevious = true;
            if (record.commandSeq <= covered)
                continue;
            MatchingEngine::Result observed;
            if (!applyRecord(engine, risk, record, error_, &observed))
                return false;
            replayedMatches_.insert(replayedMatches_.end(), observed.matches.begin(),
                                    observed.matches.end());
            ++replayedRecords_;
            recoveredCommandSeq_ = record.commandSeq;
        }
    }
    if (!engine.checkInvariants(error_) || !risk.checkInvariants(error_))
        return false;
    recoveredEventSeq_ = engine.currentEventSeq();
    destination.swapState(engine);
    destinationRisk = std::move(risk);
    return true;
}
}  // namespace lockstep
