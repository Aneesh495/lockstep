#pragma once

#include <cstdint>
#include <cstddef>
#include "lockstep/common/types.hpp"

namespace lockstep {

// Message payloads are packed for wire format
// Using pragma pack to ensure correct sizes

#pragma pack(push, 1)

// New Order message (client -> exchange)
// Size: 4+8+4+1+1+2+8+4+8+8+8 = 56 bytes
struct NewOrderPayload {
    ClientId clientId;           // 4 bytes
    OrderId orderId;             // 8 bytes
    InstrumentId instrumentId;   // 4 bytes
    Side side;                   // 1 byte
    TimeInForce tif;             // 1 byte
    std::uint8_t padding[2];     // 2 bytes
    Price price;                 // 8 bytes
    Quantity quantity;           // 4 bytes
    std::uint64_t clientSeq;     // 8 bytes
    Timestamp clientTimestamp;   // 8 bytes
    std::uint8_t reserved[8];    // 8 bytes
};
static_assert(sizeof(NewOrderPayload) == 56, "NewOrderPayload must be 56 bytes");

// Cancel Order message (client -> exchange)
// Size: 4+8+8+8+4 = 32 bytes
struct CancelOrderPayload {
    ClientId clientId;           // 4 bytes
    OrderId orderId;             // 8 bytes
    std::uint64_t clientSeq;     // 8 bytes
    Timestamp clientTimestamp;   // 8 bytes
    std::uint8_t reserved[4];    // 4 bytes
};
static_assert(sizeof(CancelOrderPayload) == 32, "CancelOrderPayload must be 32 bytes");

// Replace Order message (client -> exchange)
// Size: 4+8+8+8+4+8+8 = 48 bytes
struct ReplaceOrderPayload {
    ClientId clientId;           // 4 bytes
    OrderId oldOrderId;          // 8 bytes
    OrderId newOrderId;          // 8 bytes
    Price newPrice;              // 8 bytes
    Quantity newQuantity;        // 4 bytes
    std::uint64_t clientSeq;     // 8 bytes
    Timestamp clientTimestamp;   // 8 bytes
};
static_assert(sizeof(ReplaceOrderPayload) == 48, "ReplaceOrderPayload must be 48 bytes");

// Mass Cancel message (client -> exchange)
// Size: 4+8+8+4 = 24 bytes
struct MassCancelPayload {
    ClientId clientId;           // 4 bytes
    std::uint64_t clientSeq;     // 8 bytes
    Timestamp clientTimestamp;   // 8 bytes
    std::uint8_t reserved[4];    // 4 bytes
};
static_assert(sizeof(MassCancelPayload) == 24, "MassCancelPayload must be 24 bytes");

// Order Accepted response (exchange -> client)
// Size: 4+8+4+1+1+2+8+4+8+8+8 = 56 bytes
struct OrderAcceptedPayload {
    ClientId clientId;           // 4 bytes
    OrderId orderId;             // 8 bytes
    InstrumentId instrumentId;   // 4 bytes
    Side side;                   // 1 byte
    TimeInForce tif;             // 1 byte
    std::uint8_t padding[2];     // 2 bytes
    Price price;                 // 8 bytes
    Quantity quantity;           // 4 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
    std::uint8_t reserved[8];    // 8 bytes
};
static_assert(sizeof(OrderAcceptedPayload) == 56, "OrderAcceptedPayload must be 56 bytes");

// Order Rejected response (exchange -> client)
// Size: 4+8+2+2+8+8 = 32 bytes
struct OrderRejectedPayload {
    ClientId clientId;           // 4 bytes
    OrderId orderId;             // 8 bytes
    RejectionReason reason;      // 2 bytes
    std::uint8_t padding[2];     // 2 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
};
static_assert(sizeof(OrderRejectedPayload) == 32, "OrderRejectedPayload must be 32 bytes");

// Order Canceled response (exchange -> client)
// Size: 4+8+4+4+8+8 = 36 bytes
struct OrderCanceledPayload {
    ClientId clientId;           // 4 bytes
    OrderId orderId;             // 8 bytes
    Quantity canceledQuantity;   // 4 bytes
    Quantity remainingQuantity;  // 4 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
};
static_assert(sizeof(OrderCanceledPayload) == 36, "OrderCanceledPayload must be 36 bytes");

// Order Replaced response (exchange -> client)
// Size: 4+8+8+8+4+8+8 = 48 bytes
struct OrderReplacedPayload {
    ClientId clientId;           // 4 bytes
    OrderId oldOrderId;          // 8 bytes
    OrderId newOrderId;          // 8 bytes
    Price newPrice;              // 8 bytes
    Quantity newQuantity;        // 4 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
};
static_assert(sizeof(OrderReplacedPayload) == 48, "OrderReplacedPayload must be 48 bytes");

// Order Executed response (exchange -> client)
// Size: 4+8+8+8+4+1+1+2+8+4+4+8+8 = 68 bytes
struct OrderExecutedPayload {
    ClientId clientId;           // 4 bytes
    OrderId passiveOrderId;      // 8 bytes
    OrderId aggressiveOrderId;   // 8 bytes
    std::uint64_t matchId;       // 8 bytes
    InstrumentId instrumentId;   // 4 bytes
    Side aggressorSide;          // 1 byte
    Liquidity liquidity;         // 1 byte
    std::uint8_t padding[2];     // 2 bytes
    Price price;                 // 8 bytes
    Quantity quantity;           // 4 bytes
    Quantity passiveRemaining;   // 4 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
};
static_assert(sizeof(OrderExecutedPayload) == 68, "OrderExecutedPayload must be 68 bytes");

// Book Add event (market data)
// Size: 4+1+3+8+4+8+8 = 36 bytes
struct BookAddPayload {
    InstrumentId instrumentId;   // 4 bytes
    Side side;                   // 1 byte
    std::uint8_t padding[3];     // 3 bytes
    Price price;                 // 8 bytes
    Quantity quantity;           // 4 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
};
static_assert(sizeof(BookAddPayload) == 36, "BookAddPayload must be 36 bytes");

// Book Change event (market data)
// Size: 4+1+3+8+4+8+8 = 36 bytes
struct BookChangePayload {
    InstrumentId instrumentId;   // 4 bytes
    Side side;                   // 1 byte
    std::uint8_t padding[3];     // 3 bytes
    Price price;                 // 8 bytes
    Quantity newQuantity;        // 4 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
};
static_assert(sizeof(BookChangePayload) == 36, "BookChangePayload must be 36 bytes");

// Book Delete event (market data)
// Size: 4+1+3+8+8 = 24 bytes
struct BookDeletePayload {
    InstrumentId instrumentId;   // 4 bytes
    Side side;                   // 1 byte
    std::uint8_t padding[3];     // 3 bytes
    Price price;                 // 8 bytes
    std::uint64_t engineSeq;     // 8 bytes
};
static_assert(sizeof(BookDeletePayload) == 24, "BookDeletePayload must be 24 bytes");

// Trade event (market data)
// Size: 4+8+8+4+1+3+8+8+4 = 48 bytes
struct TradePayload {
    InstrumentId instrumentId;   // 4 bytes
    std::uint64_t matchId;       // 8 bytes
    Price price;                 // 8 bytes
    Quantity quantity;           // 4 bytes
    Side aggressorSide;          // 1 byte
    std::uint8_t padding[3];     // 3 bytes
    std::uint64_t engineSeq;     // 8 bytes
    Timestamp engineTimestamp;   // 8 bytes
    std::uint8_t reserved[4];    // 4 bytes
};
static_assert(sizeof(TradePayload) == 48, "TradePayload must be 48 bytes");

#pragma pack(pop)

} // namespace lockstep
