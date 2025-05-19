#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include "lockstep/common/endian.hpp"
#include "lockstep/common/types.hpp"
#include "lockstep/protocol/frame.hpp"
#include "lockstep/protocol/messages.hpp"

namespace lockstep {

// Codec for serializing and deserializing messages
// All integers in network byte order (big-endian)

class Codec {
   public:
    // Encode NewOrder message
    static std::size_t encodeNewOrder(const NewOrderPayload& payload, std::uint8_t* data,
                                      std::size_t size) {
        if (size < sizeof(NewOrderPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.clientId);
        writer.writeU64(payload.orderId);
        writer.writeU32(payload.instrumentId);
        writer.writeU8(static_cast<std::uint8_t>(payload.side));
        writer.writeU8(static_cast<std::uint8_t>(payload.tif));
        writer.writeBytes(payload.padding, 2);
        writer.writeI64(payload.price);
        writer.writeU32(payload.quantity);
        writer.writeU64(payload.clientSeq);
        writer.writeU64(payload.clientTimestamp);
        writer.writeBytes(payload.reserved, 8);

        return writer.pos();
    }

    // Decode NewOrder message
    static std::optional<NewOrderPayload> decodeNewOrder(const std::uint8_t* data,
                                                         std::size_t size) {
        if (size < sizeof(NewOrderPayload)) {
            return std::nullopt;
        }

        NewOrderPayload payload;
        ByteReader reader(data, size);

        ClientId clientId;
        OrderId orderId;
        InstrumentId instrumentId;
        if (!reader.readU32(clientId))
            return std::nullopt;
        if (!reader.readU64(orderId))
            return std::nullopt;
        if (!reader.readU32(instrumentId))
            return std::nullopt;
        payload.clientId = clientId;
        payload.orderId = orderId;
        payload.instrumentId = instrumentId;

        std::uint8_t side, tif;
        if (!reader.readU8(side))
            return std::nullopt;
        if (!reader.readU8(tif))
            return std::nullopt;
        payload.side = static_cast<Side>(side);
        payload.tif = static_cast<TimeInForce>(tif);

        if (!reader.skip(2))
            return std::nullopt;
        Price price;
        Quantity quantity;
        std::uint64_t clientSeq;
        Timestamp clientTimestamp;
        if (!reader.readI64(price))
            return std::nullopt;
        if (!reader.readU32(quantity))
            return std::nullopt;
        if (!reader.readU64(clientSeq))
            return std::nullopt;
        if (!reader.readU64(clientTimestamp))
            return std::nullopt;
        payload.price = price;
        payload.quantity = quantity;
        payload.clientSeq = clientSeq;
        payload.clientTimestamp = clientTimestamp;

        return payload;
    }

    // Encode CancelOrder message
    static std::size_t encodeCancelOrder(const CancelOrderPayload& payload, std::uint8_t* data,
                                         std::size_t size) {
        if (size < sizeof(CancelOrderPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.clientId);
        writer.writeU64(payload.orderId);
        writer.writeU64(payload.clientSeq);
        writer.writeU64(payload.clientTimestamp);
        writer.writeBytes(payload.reserved, 4);

        return writer.pos();
    }

    // Decode CancelOrder message
    static std::optional<CancelOrderPayload> decodeCancelOrder(const std::uint8_t* data,
                                                               std::size_t size) {
        if (size < sizeof(CancelOrderPayload)) {
            return std::nullopt;
        }

        CancelOrderPayload payload;
        ByteReader reader(data, size);

        ClientId clientId;
        OrderId orderId;
        std::uint64_t clientSeq;
        Timestamp clientTimestamp;
        if (!reader.readU32(clientId))
            return std::nullopt;
        if (!reader.readU64(orderId))
            return std::nullopt;
        if (!reader.readU64(clientSeq))
            return std::nullopt;
        if (!reader.readU64(clientTimestamp))
            return std::nullopt;
        payload.clientId = clientId;
        payload.orderId = orderId;
        payload.clientSeq = clientSeq;
        payload.clientTimestamp = clientTimestamp;

        return payload;
    }

    // Encode ReplaceOrder message
    static std::size_t encodeReplaceOrder(const ReplaceOrderPayload& payload, std::uint8_t* data,
                                          std::size_t size) {
        if (size < sizeof(ReplaceOrderPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.clientId);
        writer.writeU64(payload.oldOrderId);
        writer.writeU64(payload.newOrderId);
        writer.writeI64(payload.newPrice);
        writer.writeU32(payload.newQuantity);
        writer.writeU64(payload.clientSeq);
        writer.writeU64(payload.clientTimestamp);

        return writer.pos();
    }

    // Decode ReplaceOrder message
    static std::optional<ReplaceOrderPayload> decodeReplaceOrder(const std::uint8_t* data,
                                                                 std::size_t size) {
        if (size < sizeof(ReplaceOrderPayload)) {
            return std::nullopt;
        }

        ReplaceOrderPayload payload;
        ByteReader reader(data, size);

        ClientId clientId;
        OrderId oldOrderId;
        OrderId newOrderId;
        Price newPrice;
        Quantity newQuantity;
        std::uint64_t clientSeq;
        Timestamp clientTimestamp;
        if (!reader.readU32(clientId))
            return std::nullopt;
        if (!reader.readU64(oldOrderId))
            return std::nullopt;
        if (!reader.readU64(newOrderId))
            return std::nullopt;
        if (!reader.readI64(newPrice))
            return std::nullopt;
        if (!reader.readU32(newQuantity))
            return std::nullopt;
        if (!reader.readU64(clientSeq))
            return std::nullopt;
        if (!reader.readU64(clientTimestamp))
            return std::nullopt;
        payload.clientId = clientId;
        payload.oldOrderId = oldOrderId;
        payload.newOrderId = newOrderId;
        payload.newPrice = newPrice;
        payload.newQuantity = newQuantity;
        payload.clientSeq = clientSeq;
        payload.clientTimestamp = clientTimestamp;

        return payload;
    }

    // Encode MassCancel message
    static std::size_t encodeMassCancel(const MassCancelPayload& payload, std::uint8_t* data,
                                        std::size_t size) {
        if (size < sizeof(MassCancelPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.clientId);
        writer.writeU64(payload.clientSeq);
        writer.writeU64(payload.clientTimestamp);
        writer.writeBytes(payload.reserved, 4);

        return writer.pos();
    }

    // Decode MassCancel message
    static std::optional<MassCancelPayload> decodeMassCancel(const std::uint8_t* data,
                                                             std::size_t size) {
        if (size < sizeof(MassCancelPayload)) {
            return std::nullopt;
        }

        MassCancelPayload payload;
        ByteReader reader(data, size);

        ClientId clientId;
        std::uint64_t clientSeq;
        Timestamp clientTimestamp;
        if (!reader.readU32(clientId))
            return std::nullopt;
        if (!reader.readU64(clientSeq))
            return std::nullopt;
        if (!reader.readU64(clientTimestamp))
            return std::nullopt;
        payload.clientId = clientId;
        payload.clientSeq = clientSeq;
        payload.clientTimestamp = clientTimestamp;

        return payload;
    }

    // Encode OrderAccepted response
    static std::size_t encodeOrderAccepted(const OrderAcceptedPayload& payload, std::uint8_t* data,
                                           std::size_t size) {
        if (size < sizeof(OrderAcceptedPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.clientId);
        writer.writeU64(payload.orderId);
        writer.writeU32(payload.instrumentId);
        writer.writeU8(static_cast<std::uint8_t>(payload.side));
        writer.writeU8(static_cast<std::uint8_t>(payload.tif));
        writer.writeBytes(payload.padding, 2);
        writer.writeI64(payload.price);
        writer.writeU32(payload.quantity);
        writer.writeU64(payload.engineSeq);
        writer.writeU64(payload.engineTimestamp);
        writer.writeBytes(payload.reserved, 8);

        return writer.pos();
    }

    // Encode OrderRejected response
    static std::size_t encodeOrderRejected(const OrderRejectedPayload& payload, std::uint8_t* data,
                                           std::size_t size) {
        if (size < sizeof(OrderRejectedPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.clientId);
        writer.writeU64(payload.orderId);
        writer.writeU16(static_cast<std::uint16_t>(payload.reason));
        writer.writeBytes(payload.padding, 2);
        writer.writeU64(payload.engineSeq);
        writer.writeU64(payload.engineTimestamp);

        return writer.pos();
    }

    // Encode OrderExecuted response
    static std::size_t encodeOrderExecuted(const OrderExecutedPayload& payload, std::uint8_t* data,
                                           std::size_t size) {
        if (size < sizeof(OrderExecutedPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.clientId);
        writer.writeU64(payload.passiveOrderId);
        writer.writeU64(payload.aggressiveOrderId);
        writer.writeU64(payload.matchId);
        writer.writeU32(payload.instrumentId);
        writer.writeU8(static_cast<std::uint8_t>(payload.aggressorSide));
        writer.writeU8(static_cast<std::uint8_t>(payload.liquidity));
        writer.writeBytes(payload.padding, 2);
        writer.writeI64(payload.price);
        writer.writeU32(payload.quantity);
        writer.writeU32(payload.passiveRemaining);
        writer.writeU64(payload.engineSeq);
        writer.writeU64(payload.engineTimestamp);

        return writer.pos();
    }

    // Encode book events
    static std::size_t encodeBookAdd(const BookAddPayload& payload, std::uint8_t* data,
                                     std::size_t size) {
        if (size < sizeof(BookAddPayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.instrumentId);
        writer.writeU8(static_cast<std::uint8_t>(payload.side));
        writer.writeBytes(payload.padding, 3);
        writer.writeI64(payload.price);
        writer.writeU32(payload.quantity);
        writer.writeU64(payload.engineSeq);
        writer.writeU64(payload.engineTimestamp);

        return writer.pos();
    }

    static std::size_t encodeBookChange(const BookChangePayload& payload, std::uint8_t* data,
                                        std::size_t size) {
        if (size < sizeof(BookChangePayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.instrumentId);
        writer.writeU8(static_cast<std::uint8_t>(payload.side));
        writer.writeBytes(payload.padding, 3);
        writer.writeI64(payload.price);
        writer.writeU32(payload.newQuantity);
        writer.writeU64(payload.engineSeq);
        writer.writeU64(payload.engineTimestamp);

        return writer.pos();
    }

    static std::size_t encodeBookDelete(const BookDeletePayload& payload, std::uint8_t* data,
                                        std::size_t size) {
        if (size < sizeof(BookDeletePayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.instrumentId);
        writer.writeU8(static_cast<std::uint8_t>(payload.side));
        writer.writeBytes(payload.padding, 3);
        writer.writeI64(payload.price);
        writer.writeU64(payload.engineSeq);

        return writer.pos();
    }

    static std::size_t encodeTrade(const TradePayload& payload, std::uint8_t* data,
                                   std::size_t size) {
        if (size < sizeof(TradePayload)) {
            return 0;
        }

        ByteWriter writer(data, size);
        writer.writeU32(payload.instrumentId);
        writer.writeU64(payload.matchId);
        writer.writeI64(payload.price);
        writer.writeU32(payload.quantity);
        writer.writeU8(static_cast<std::uint8_t>(payload.aggressorSide));
        writer.writeBytes(payload.padding, 3);
        writer.writeU64(payload.engineSeq);
        writer.writeU64(payload.engineTimestamp);
        writer.writeBytes(payload.reserved, 4);

        return writer.pos();
    }

    // Decode OrderAccepted response
    static std::optional<OrderAcceptedPayload> decodeOrderAccepted(const std::uint8_t* data,
                                                                   std::size_t size) {
        if (size < sizeof(OrderAcceptedPayload))
            return std::nullopt;
        OrderAcceptedPayload p;
        ByteReader r(data, size);
        ClientId clientId;
        OrderId orderId;
        InstrumentId instrumentId;
        std::uint8_t side, tif;
        Price price;
        Quantity quantity;
        std::uint64_t engineSeq, engineTimestamp;
        if (!r.readU32(clientId) || !r.readU64(orderId) || !r.readU32(instrumentId))
            return std::nullopt;
        if (!r.readU8(side) || !r.readU8(tif) || !r.skip(2))
            return std::nullopt;
        if (!r.readI64(price) || !r.readU32(quantity) || !r.readU64(engineSeq) ||
            !r.readU64(engineTimestamp))
            return std::nullopt;
        p.clientId = clientId;
        p.orderId = orderId;
        p.instrumentId = instrumentId;
        p.side = static_cast<Side>(side);
        p.tif = static_cast<TimeInForce>(tif);
        p.price = price;
        p.quantity = quantity;
        p.engineSeq = engineSeq;
        p.engineTimestamp = engineTimestamp;
        return p;
    }

    // Decode OrderRejected response
    static std::optional<OrderRejectedPayload> decodeOrderRejected(const std::uint8_t* data,
                                                                   std::size_t size) {
        if (size < sizeof(OrderRejectedPayload))
            return std::nullopt;
        OrderRejectedPayload p;
        ByteReader r(data, size);
        ClientId clientId;
        OrderId orderId;
        std::uint16_t reason;
        std::uint64_t engineSeq, engineTimestamp;
        if (!r.readU32(clientId) || !r.readU64(orderId) || !r.readU16(reason) || !r.skip(2) ||
            !r.readU64(engineSeq) || !r.readU64(engineTimestamp))
            return std::nullopt;
        p.clientId = clientId;
        p.orderId = orderId;
        p.reason = static_cast<RejectionReason>(reason);
        p.engineSeq = engineSeq;
        p.engineTimestamp = engineTimestamp;
        return p;
    }

    // Decode OrderExecuted response
    static std::optional<OrderExecutedPayload> decodeOrderExecuted(const std::uint8_t* data,
                                                                   std::size_t size) {
        if (size < sizeof(OrderExecutedPayload))
            return std::nullopt;
        OrderExecutedPayload p;
        ByteReader r(data, size);
        ClientId clientId;
        OrderId passiveOrderId, aggressiveOrderId;
        std::uint64_t matchId;
        InstrumentId instrumentId;
        std::uint8_t aggressorSide, liquidity;
        Price price;
        Quantity quantity, passiveRemaining;
        std::uint64_t engineSeq, engineTimestamp;
        if (!r.readU32(clientId) || !r.readU64(passiveOrderId) || !r.readU64(aggressiveOrderId) ||
            !r.readU64(matchId) || !r.readU32(instrumentId) || !r.readU8(aggressorSide) ||
            !r.readU8(liquidity) || !r.skip(2) || !r.readI64(price) || !r.readU32(quantity) ||
            !r.readU32(passiveRemaining) || !r.readU64(engineSeq) || !r.readU64(engineTimestamp))
            return std::nullopt;
        p.clientId = clientId;
        p.passiveOrderId = passiveOrderId;
        p.aggressiveOrderId = aggressiveOrderId;
        p.matchId = matchId;
        p.instrumentId = instrumentId;
        p.aggressorSide = static_cast<Side>(aggressorSide);
        p.liquidity = static_cast<Liquidity>(liquidity);
        p.price = price;
        p.quantity = quantity;
        p.passiveRemaining = passiveRemaining;
        p.engineSeq = engineSeq;
        p.engineTimestamp = engineTimestamp;
        return p;
    }

    // Decode BookAdd event
    static std::optional<BookAddPayload> decodeBookAdd(const std::uint8_t* data, std::size_t size) {
        if (size < sizeof(BookAddPayload))
            return std::nullopt;
        BookAddPayload p;
        ByteReader r(data, size);
        InstrumentId instrumentId;
        std::uint8_t side;
        Price price;
        Quantity quantity;
        std::uint64_t engineSeq, engineTimestamp;
        if (!r.readU32(instrumentId) || !r.readU8(side) || !r.skip(3) || !r.readI64(price) ||
            !r.readU32(quantity) || !r.readU64(engineSeq) || !r.readU64(engineTimestamp))
            return std::nullopt;
        p.instrumentId = instrumentId;
        p.side = static_cast<Side>(side);
        p.price = price;
        p.quantity = quantity;
        p.engineSeq = engineSeq;
        p.engineTimestamp = engineTimestamp;
        return p;
    }

    // Decode BookChange event
    static std::optional<BookChangePayload> decodeBookChange(const std::uint8_t* data,
                                                             std::size_t size) {
        if (size < sizeof(BookChangePayload))
            return std::nullopt;
        BookChangePayload p;
        ByteReader r(data, size);
        InstrumentId instrumentId;
        std::uint8_t side;
        Price price;
        Quantity newQuantity;
        std::uint64_t engineSeq, engineTimestamp;
        if (!r.readU32(instrumentId) || !r.readU8(side) || !r.skip(3) || !r.readI64(price) ||
            !r.readU32(newQuantity) || !r.readU64(engineSeq) || !r.readU64(engineTimestamp))
            return std::nullopt;
        p.instrumentId = instrumentId;
        p.side = static_cast<Side>(side);
        p.price = price;
        p.newQuantity = newQuantity;
        p.engineSeq = engineSeq;
        p.engineTimestamp = engineTimestamp;
        return p;
    }

    // Decode BookDelete event
    static std::optional<BookDeletePayload> decodeBookDelete(const std::uint8_t* data,
                                                             std::size_t size) {
        if (size < sizeof(BookDeletePayload))
            return std::nullopt;
        BookDeletePayload p;
        ByteReader r(data, size);
        InstrumentId instrumentId;
        std::uint8_t side;
        Price price;
        std::uint64_t engineSeq;
        if (!r.readU32(instrumentId) || !r.readU8(side) || !r.skip(3) || !r.readI64(price) ||
            !r.readU64(engineSeq))
            return std::nullopt;
        p.instrumentId = instrumentId;
        p.side = static_cast<Side>(side);
        p.price = price;
        p.engineSeq = engineSeq;
        return p;
    }

    // Decode Trade event
    static std::optional<TradePayload> decodeTrade(const std::uint8_t* data, std::size_t size) {
        if (size < sizeof(TradePayload))
            return std::nullopt;
        TradePayload p;
        ByteReader r(data, size);
        InstrumentId instrumentId;
        std::uint64_t matchId;
        Price price;
        Quantity quantity;
        std::uint8_t aggressorSide;
        std::uint64_t engineSeq, engineTimestamp;
        if (!r.readU32(instrumentId) || !r.readU64(matchId) || !r.readI64(price) ||
            !r.readU32(quantity) || !r.readU8(aggressorSide) || !r.skip(3) ||
            !r.readU64(engineSeq) || !r.readU64(engineTimestamp))
            return std::nullopt;
        p.instrumentId = instrumentId;
        p.matchId = matchId;
        p.price = price;
        p.quantity = quantity;
        p.aggressorSide = static_cast<Side>(aggressorSide);
        p.engineSeq = engineSeq;
        p.engineTimestamp = engineTimestamp;
        return p;
    }
};

}  // namespace lockstep
