#pragma once

#include <cstdint>
#include "lockstep/common/types.hpp"

namespace lockstep {

// Order structure stored in the object pool
// Uses intrusive prev/next for FIFO priority

struct Order {
    // Identity
    OrderId orderId = INVALID_ORDER_ID;
    ClientId clientId = INVALID_CLIENT_ID;
    InstrumentId instrumentId = INVALID_INSTRUMENT_ID;
    
    // Order details
    Side side = Side::Buy;
    TimeInForce tif = TimeInForce::GTC;
    OrderStatus status = OrderStatus::Pending;
    
    // Price and quantity
    Price price = 0;
    Quantity quantity = 0;
    Quantity executedQuantity = 0;
    Quantity remainingQuantity() const { return quantity - executedQuantity; }
    
    // Sequencing
    std::uint64_t clientSeq = 0;
    std::uint64_t engineSeq = 0;
    
    // Timestamps
    Timestamp createTime = 0;
    Timestamp updateTime = 0;
    
    // Intrusive FIFO linkage (indices into pool)
    SlotIndex prev = INVALID_SLOT;
    SlotIndex next = INVALID_SLOT;
    PriceOffset priceLevelOffset = INVALID_PRICE_OFFSET;
    
    // For accounting
    bool reserved = false;
    Notional reservedNotional = 0;
    
    // Check if order is active
    bool isActive() const {
        return status == OrderStatus::Live || status == OrderStatus::PartiallyFilled;
    }
    
    // Check if order is terminal
    bool isTerminal() const {
        return status == OrderStatus::Filled ||
               status == OrderStatus::Canceled ||
               status == OrderStatus::Rejected ||
               status == OrderStatus::Expired;
    }
    
    // Reset order to default state (for reuse)
    void reset() {
        orderId = INVALID_ORDER_ID;
        clientId = INVALID_CLIENT_ID;
        instrumentId = INVALID_INSTRUMENT_ID;
        side = Side::Buy;
        tif = TimeInForce::GTC;
        status = OrderStatus::Pending;
        price = 0;
        quantity = 0;
        executedQuantity = 0;
        clientSeq = 0;
        engineSeq = 0;
        createTime = 0;
        updateTime = 0;
        prev = INVALID_SLOT;
        next = INVALID_SLOT;
        priceLevelOffset = INVALID_PRICE_OFFSET;
        reserved = false;
        reservedNotional = 0;
    }
};

} // namespace lockstep
