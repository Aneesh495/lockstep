#include "lockstep/persistence/recovery.hpp"
#include <cstring>
#include <filesystem>
#include "lockstep/protocol/codec.hpp"
#include "lockstep/protocol/frame.hpp"

namespace lockstep {

RecoveryManager::RecoveryManager(const std::string& snapshotPath, const std::string& walPath)
    : snapshotPath_(snapshotPath), walPath_(walPath) {}

bool RecoveryManager::applyRecord(MatchingEngine& engine, RiskEngine& risk, const WalRecord& record,
                                  std::string& error) {
    const std::uint8_t* payloadData = record.payload.data();
    std::size_t payloadSize = record.payload.size();

    // Check if payload is framed with LKST header
    MessageType msgType = static_cast<MessageType>(record.recordKind);
    if (payloadSize >= FRAME_HEADER_SIZE && std::memcmp(payloadData, "LKST", 4) == 0) {
        auto frameHdr = FrameHeader::parse(payloadData, payloadSize);
        if (frameHdr) {
            msgType = frameHdr->messageType();
            payloadData += FRAME_HEADER_SIZE;
            payloadSize = frameHdr->payloadLength();
        }
    }

    if (msgType == MessageType::NewOrder) {
        auto dec = Codec::decodeNewOrder(payloadData, payloadSize);
        if (!dec) {
            error = "Failed to decode NewOrderPayload";
            return false;
        }

        Order order;
        order.clientId = dec->clientId;
        order.orderId = dec->orderId;
        order.instrumentId = dec->instrumentId;
        order.side = dec->side;
        order.tif = dec->tif;
        order.price = dec->price;
        order.quantity = dec->quantity;
        order.clientSeq = dec->clientSeq;

        const InstrumentConfig* cfg = engine.getInstrumentConfig(order.instrumentId);
        if (cfg == nullptr) {
            error = "Unknown instrument " + std::to_string(order.instrumentId);
            return false;
        }

        if (risk.getClientState(order.clientId) == nullptr) {
            RiskLimits limits;
            limits.clientId = order.clientId;
            risk.setClientLimits(order.clientId, limits);
        }

        auto [riskOk, reason] = risk.checkNewOrder(order.clientId, order.instrumentId, order.side,
                                                   order.price, order.quantity, *cfg);
        if (!riskOk) {
            engine.nextCommandSeq();
            return true;
        }

        auto result = engine.newOrder(order);
        if (result.success) {
            Quantity executedQty = 0;
            for (const auto& match : result.matches) {
                executedQty += match.quantity;
                if (risk.getClientState(match.passiveClientId) == nullptr) {
                    RiskLimits limits;
                    limits.clientId = match.passiveClientId;
                    risk.setClientLimits(match.passiveClientId, limits);
                }
                Side aggSide = order.side;
                Side passSide = (aggSide == Side::Buy) ? Side::Sell : Side::Buy;
                risk.updatePosition(match.aggressiveClientId, order.instrumentId, aggSide,
                                    match.quantity);
                risk.updatePosition(match.passiveClientId, order.instrumentId, passSide,
                                    match.quantity);
            }
            Quantity remQty = (order.quantity > executedQty) ? (order.quantity - executedQty) : 0;
            if (remQty > 0 && order.tif == TimeInForce::GTC) {
                risk.reserveOrder(order.clientId, order.instrumentId, order.side, order.price,
                                  remQty);
            }
        }
        return true;
    }

    if (msgType == MessageType::CancelOrder) {
        auto dec = Codec::decodeCancelOrder(payloadData, payloadSize);
        if (!dec) {
            error = "Failed to decode CancelOrderPayload";
            return false;
        }

        Order restingOrder;
        bool found = false;
        for (const auto& cfg : engine.instrumentConfigs()) {
            OrderBook* b = engine.getBook(cfg.id);
            if (b != nullptr) {
                auto oOpt = b->findOrder(dec->clientId, dec->orderId);
                if (oOpt) {
                    restingOrder = oOpt->get();
                    found = true;
                    break;
                }
            }
        }

        auto result = engine.cancelOrder(dec->clientId, dec->orderId);
        if (result.success && found) {
            risk.releaseOrder(restingOrder.clientId, restingOrder.instrumentId, restingOrder.side,
                              restingOrder.price, restingOrder.remainingQuantity());
        }
        return true;
    }

    if (msgType == MessageType::ReplaceOrder) {
        auto dec = Codec::decodeReplaceOrder(payloadData, payloadSize);
        if (!dec) {
            error = "Failed to decode ReplaceOrderPayload";
            return false;
        }

        Order restingOrder;
        bool found = false;
        for (const auto& cfg : engine.instrumentConfigs()) {
            OrderBook* b = engine.getBook(cfg.id);
            if (b != nullptr) {
                auto oOpt = b->findOrder(dec->clientId, dec->oldOrderId);
                if (oOpt) {
                    restingOrder = oOpt->get();
                    found = true;
                    break;
                }
            }
        }

        auto result =
            engine.replaceOrder(dec->clientId, dec->oldOrderId, dec->newOrderId,
                                restingOrder.instrumentId, dec->newPrice, dec->newQuantity);
        if (result.success && found) {
            risk.releaseOrder(restingOrder.clientId, restingOrder.instrumentId, restingOrder.side,
                              restingOrder.price, restingOrder.remainingQuantity());
            risk.reserveOrder(dec->clientId, restingOrder.instrumentId, restingOrder.side,
                              dec->newPrice, dec->newQuantity);
        }
        return true;
    }

    if (msgType == MessageType::MassCancel) {
        auto dec = Codec::decodeMassCancel(payloadData, payloadSize);
        if (!dec) {
            error = "Failed to decode MassCancelPayload";
            return false;
        }

        engine.massCancel(dec->clientId);
        risk.onMassCancel(dec->clientId);
        return true;
    }

    error = "Unsupported record kind or message type: " + std::to_string(static_cast<int>(msgType));
    return false;
}

bool RecoveryManager::recover(MatchingEngine& engine, RiskEngine& risk) {
    // 1. Try to load snapshot
    if (!snapshotPath_.empty() && std::filesystem::exists(snapshotPath_)) {
        SnapshotReader reader(snapshotPath_);
        if (!reader.read(engine, risk)) {
            error_ = "Failed to recover from snapshot: " + reader.error();
            return false;
        }
        recoveredCommandSeq_ = reader.coveredWalSeq();
        recoveredEventSeq_ = reader.eventSeq();
    }

    // 2. Replay WAL
    if (!walPath_.empty() && std::filesystem::exists(walPath_)) {
        WalReader walReader(walPath_);
        if (!walReader.isOpen()) {
            error_ = "Cannot open WAL: " + walPath_;
            return false;
        }

        std::uint64_t snapshotCoveredSeq = recoveredCommandSeq_;
        std::uint64_t expectedSeq = snapshotCoveredSeq + 1;
        bool firstRecord = true;

        while (true) {
            WalRecord record;
            WalStatus status = walReader.readRecord(record);

            if (status == WalStatus::CleanEof) {
                break;
            }

            if (status == WalStatus::IncompleteTail) {
                // Recoverable final-tail case
                finalTailIncomplete_ = true;
                break;
            }

            if (status != WalStatus::Ok) {
                error_ = "WAL replay error: " + walReader.error();
                return false;
            }

            // Skip records before or included in snapshot
            if (record.commandSeq <= snapshotCoveredSeq) {
                continue;
            }

            if (firstRecord && snapshotCoveredSeq == 0) {
                expectedSeq = record.commandSeq;
                firstRecord = false;
            }

            if (record.commandSeq < expectedSeq) {
                error_ = "Duplicate command sequence in WAL: expected " +
                         std::to_string(expectedSeq) + " got " + std::to_string(record.commandSeq);
                return false;
            }

            if (record.commandSeq > expectedSeq) {
                error_ = "Missing command sequence gap in WAL: expected " +
                         std::to_string(expectedSeq) + " got " + std::to_string(record.commandSeq);
                return false;
            }

            expectedSeq = record.commandSeq + 1;

            std::string applyErr;
            if (!applyRecord(engine, risk, record, applyErr)) {
                error_ = "Failed to apply WAL command at seq " + std::to_string(record.commandSeq) +
                         ": " + applyErr;
                return false;
            }

            replayedRecords_++;
            recoveredCommandSeq_ = record.commandSeq;
            recoveredEventSeq_ = engine.currentEventSeq();
        }
    }

    if (!engine.checkInvariants(error_)) {
        return false;
    }

    if (!risk.checkInvariants(error_)) {
        return false;
    }

    return true;
}

}  // namespace lockstep
