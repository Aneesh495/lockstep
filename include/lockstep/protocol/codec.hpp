#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <optional>
#include <variant>
#include "lockstep/common/types.hpp"
#include "lockstep/common/endian.hpp"
#include "lockstep/protocol/frame.hpp"
#include "lockstep/protocol/messages.hpp"

namespace lockstep {

// Codec for serializing and deserializing messages
// All integers in network byte order (big-endian)

class Codec {
public:
    // Encode NewOrder message
    static std::size_t encodeNewOrder(const NewOrderPayload& payload, std::uint8_t* data, std::size_t size) {
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
    static std::optional<NewOrderPayload> decodeNewOrder(const std::uint8_t* data, std::size_t size) {
        if (size < sizeof(NewOrderPayload)) {
            return std::nullopt;
        }
        
        NewOrderPayload payload;
        ByteReader reader(data, size);
        
        if (!reader.readU32(payload.clientId)) return std::nullopt;
        if (!reader.readU64(payload.orderId)) return std::nullopt;
        if (!reader.readU32(payload.instrumentId)) return std::nullopt;
        
        std::uint8_t side, tif;
        if (!reader.readU8(side)) return std::nullopt;
        if (!reader.readU8(tif)) return std::nullopt;
        payload.side = static_cast<Side>(side);
        payload.tif = static_cast<TimeInForce>(tif);
        
        if (!reader.skip(2)) return std::nullopt;
        if (!reader.readI64(payload.price)) return std::nullopt;
        if (!reader.readU32(payload.quantity)) return std::nullopt;
        if (!reader.readU64(payload.clientSeq)) return std::nullopt;
        if (!reader.readU64(payload.clientTimestamp)) return std::nullopt;
        
        return payload;
    }
    
    // Encode CancelOrder message
    static std::size_t encodeCancelOrder(const CancelOrderPayload& payload, std::uint8_t* data, std::size_t size) {
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
    static std::optional<CancelOrderPayload> decodeCancelOrder(const std::uint8_t* data, std::size_t size) {
        if (size < sizeof(CancelOrderPayload)) {
            return std::nullopt;
        }
        
        CancelOrderPayload payload;
        ByteReader reader(data, size);
        
        if (!reader.readU32(payload.clientId)) return std::nullopt;
        if (!reader.readU64(payload.orderId)) return std::nullopt;
        if (!reader.readU64(payload.clientSeq)) return std::nullopt;
        if (!reader.readU64(payload.clientTimestamp)) return std::nullopt;
        
        return payload;
    }
    
    // Encode ReplaceOrder message
    static std::size_t encodeReplaceOrder(const ReplaceOrderPayload& payload, std::uint8_t* data, std::size_t size) {
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
    static std::optional<ReplaceOrderPayload> decodeReplaceOrder(const std::uint8_t* data, std::size_t size) {
        if (size < sizeof(ReplaceOrderPayload)) {
            return std::nullopt;
        }
        
        ReplaceOrderPayload payload;
        ByteReader reader(data, size);
        
        if (!reader.readU32(payload.clientId)) return std::nullopt;
        if (!reader.readU64(payload.oldOrderId)) return std::nullopt;
        if (!reader.readU64(payload.newOrderId)) return std::nullopt;
        if (!reader.readI64(payload.newPrice)) return std::nullopt;
        if (!reader.readU32(payload.newQuantity)) return std::nullopt;
        if (!reader.readU64(payload.clientSeq)) return std::nullopt;
        if (!reader.readU64(payload.clientTimestamp)) return std::nullopt;
        
        return payload;
    }
    
    // Encode OrderAccepted response
    static std::size_t encodeOrderAccepted(const OrderAcceptedPayload& payload, std::uint8_t* data, std::size_t size) {
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
    static std::size_t encodeOrderRejected(const OrderRejectedPayload& payload, std::uint8_t* data, std::size_t size) {
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
    static std::size_t encodeOrderExecuted(const OrderExecutedPayload& payload, std::uint8_t* data, std::size_t size) {
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
    static std::size_t encodeBookAdd(const BookAddPayload& payload, std::uint8_t* data, std::size_t size) {
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
    
    static std::size_t encodeBookChange(const BookChangePayload& payload, std::uint8_t* data, std::size_t size) {
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
    
    static std::size_t encodeBookDelete(const BookDeletePayload& payload, std::uint8_t* data, std::size_t size) {
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
    
    static std::size_t encodeTrade(const TradePayload& payload, std::uint8_t* data, std::size_t size) {
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
};

} // namespace lockstep
