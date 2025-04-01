#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace lockstep {

// Core numeric types as specified
using Price = std::int64_t;          // Signed 64-bit integer ticks
using Quantity = std::uint32_t;      // Unsigned 32-bit integer lots
using Position = std::int64_t;       // Signed 64-bit positions
using Notional = __int128;           // Checked 128-bit intermediate for notional arithmetic

using ClientId = std::uint32_t;
using OrderId = std::uint64_t;
using InstrumentId = std::uint32_t;
using Sequence = std::uint64_t;
using Timestamp = std::uint64_t;     // Monotonic integer nanoseconds

using SlotIndex = std::uint32_t;
using PriceOffset = std::uint32_t;

// Invalid/sentinel values
constexpr ClientId INVALID_CLIENT_ID = std::numeric_limits<ClientId>::max();
constexpr OrderId INVALID_ORDER_ID = std::numeric_limits<OrderId>::max();
constexpr InstrumentId INVALID_INSTRUMENT_ID = std::numeric_limits<InstrumentId>::max();
constexpr Sequence INVALID_SEQUENCE = std::numeric_limits<Sequence>::max();
constexpr Timestamp INVALID_TIMESTAMP = std::numeric_limits<Timestamp>::max();
constexpr SlotIndex INVALID_SLOT = std::numeric_limits<SlotIndex>::max();
constexpr PriceOffset INVALID_PRICE_OFFSET = std::numeric_limits<PriceOffset>::max();

// Network byte order magic
constexpr std::uint32_t FRAME_MAGIC = 0x4C4B5354; // "LKST"
constexpr std::uint8_t PROTOCOL_VERSION = 1;

// Time in Force
enum class TimeInForce : std::uint8_t {
    GTC = 0,  // Good Till Cancel
    IOC = 1,  // Immediate or Cancel
    FOK = 2,  // Fill or Kill
};

// Side
enum class Side : std::uint8_t {
    Buy = 0,
    Sell = 1,
};

// Order status
enum class OrderStatus : std::uint8_t {
    Pending = 0,
    Live = 1,
    PartiallyFilled = 2,
    Filled = 3,
    Canceled = 4,
    Rejected = 5,
    Expired = 6,
};

// Rejection reason codes (stable)
enum class RejectionReason : std::uint16_t {
    None = 0,
    UnknownInstrument = 1,
    InvalidPrice = 2,
    InvalidQuantity = 3,
    InvalidSide = 4,
    InvalidTimeInForce = 5,
    DuplicateOrderId = 6,
    OrderNotFound = 7,
    OrderNotLive = 8,
    InsufficientQuantity = 9,
    MaxOrderQuantityExceeded = 10,
    MaxOrderNotionalExceeded = 11,
    MaxOpenOrdersExceeded = 12,
    MaxOpenQuantityExceeded = 13,
    MaxOpenNotionalExceeded = 14,
    MaxPositionExceeded = 15,
    KillSwitchActive = 16,
    PriceBandViolation = 17,
    SelfTradePrevention = 18,
    FOKCannotFill = 19,
    SequenceTooOld = 20,
    InstrumentSuspended = 21,
    CapacityExceeded = 22,
    InvalidMessage = 23,
    InternalError = 24,
};

// Message types
enum class MessageType : std::uint8_t {
    // Client -> Exchange
    NewOrder = 1,
    CancelOrder = 2,
    ReplaceOrder = 3,
    MassCancel = 4,
    SnapshotRequest = 5,
    Heartbeat = 6,
    
    // Exchange -> Client
    OrderAccepted = 101,
    OrderRejected = 102,
    OrderCanceled = 103,
    OrderReplaced = 104,
    OrderExecuted = 105,
    MassCancelDone = 106,
    SnapshotBegin = 107,
    SnapshotRow = 108,
    SnapshotEnd = 109,
    HeartbeatAck = 110,
    
    // Market data events
    BookAdd = 201,
    BookChange = 202,
    BookDelete = 203,
    Trade = 204,
    TradingStatus = 205,
};

// Trading status
enum class TradingStatus : std::uint8_t {
    Open = 0,
    Halted = 1,
    Closed = 2,
    PreOpen = 3,
    PreClose = 4,
};

// Liquidity indicator
enum class Liquidity : std::uint8_t {
    Passive = 0,
    Aggressive = 1,
};

constexpr std::size_t FRAME_HEADER_SIZE = 40;
constexpr std::size_t MAX_PAYLOAD_SIZE = 1400; // MTU-safe

// Instrument configuration
struct InstrumentConfig {
    InstrumentId id = INVALID_INSTRUMENT_ID;
    Price minPrice = 0;
    Price maxPrice = 0;
    Price tickSize = 1;
    Quantity maxQuantity = std::numeric_limits<Quantity>::max();
    std::uint32_t maxOrdersPerLevel = 10000;
    std::uint32_t maxPriceLevels = 10000;
};

// Risk limits per client
struct RiskLimits {
    ClientId clientId = INVALID_CLIENT_ID;
    Quantity maxOrderQuantity = std::numeric_limits<Quantity>::max();
    Notional maxOrderNotional = 0;
    std::uint32_t maxOpenOrders = 1000;
    Quantity maxOpenQuantity = std::numeric_limits<Quantity>::max();
    Notional maxOpenNotional = 0;
    Position maxPosition = std::numeric_limits<Position>::max();
    bool killSwitchActive = false;
};

// Match result for a single execution
struct Match {
    OrderId passiveOrderId = INVALID_ORDER_ID;
    OrderId aggressiveOrderId = INVALID_ORDER_ID;
    std::uint64_t matchId = 0;
    Price price = 0;
    Quantity quantity = 0;
    Liquidity aggressiveLiquidity = Liquidity::Aggressive;
    Timestamp timestamp = 0;
};

} // namespace lockstep
