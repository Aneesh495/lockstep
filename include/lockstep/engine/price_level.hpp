#pragma once

#include <cstdint>
#include "lockstep/common/types.hpp"

namespace lockstep {

// Price level with FIFO queue of orders
// Uses slot indices for intrusive linkage

struct PriceLevel {
    Price price = 0;
    std::uint32_t orderCount = 0;
    Quantity totalQuantity = 0;
    
    // FIFO queue head and tail (indices into order pool)
    SlotIndex head = INVALID_SLOT;
    SlotIndex tail = INVALID_SLOT;
    
    // Occupancy bitset position (for quick lookup)
    PriceOffset priceOffset = INVALID_PRICE_OFFSET;
    
    // Check if level is empty
    bool empty() const { return orderCount == 0; }
    
    // Reset to default state
    void reset() {
        price = 0;
        orderCount = 0;
        totalQuantity = 0;
        head = INVALID_SLOT;
        tail = INVALID_SLOT;
    }
};

} // namespace lockstep
