#pragma once

#include <cstdint>
#include <vector>
#include <bitset>
#include <optional>
#include "lockstep/common/types.hpp"
#include "lockstep/common/checked_math.hpp"
#include "lockstep/containers/object_pool.hpp"
#include "lockstep/containers/fixed_robin_hood_map.hpp"
#include "lockstep/engine/order.hpp"
#include "lockstep/engine/price_level.hpp"

namespace lockstep {

// Optimized price-time order book
// Fixed-capacity, zero heap allocations after initialization
// Preallocated price levels indexed by legal price offset

class OrderBook {
public:
    // Result of an operation
    struct Result {
        bool success = false;
        RejectionReason reason = RejectionReason::None;
        Quantity filledQuantity = 0;
        std::vector<Match> matches;
    };
    
    // Configuration
    struct Config {
        InstrumentId instrumentId = INVALID_INSTRUMENT_ID;
        Price minPrice = 0;
        Price maxPrice = 1000000;
        Price tickSize = 1;
        std::uint32_t maxOrders = 100000;
        std::uint32_t maxOrdersPerLevel = 1000;
    };
    
    explicit OrderBook(const Config& config);
    
    // Order operations
    Result newOrder(Order& order);
    Result cancelOrder(ClientId clientId, OrderId orderId);
    Result replaceOrder(ClientId clientId, OrderId oldOrderId, OrderId newOrderId,
                       Price newPrice, Quantity newQuantity);
    std::uint32_t massCancel(ClientId clientId);
    
    // Access
    std::optional<std::reference_wrapper<Order>> findOrder(ClientId clientId, OrderId orderId);
    std::optional<std::reference_wrapper<const Order>> findOrder(ClientId clientId, OrderId orderId) const;
    
    // Market data
    Price bestBid() const;
    Price bestAsk() const;
    Quantity bidQuantity(Price price) const;
    Quantity askQuantity(Price price) const;
    
    // State
    std::uint32_t orderCount() const { return orderPool_.size(); }
    bool empty() const { return orderPool_.empty(); }
    
    // Serialization for snapshots
    template <typename Func>
    void forEachOrder(Func&& func) const {
        orderPool_.forEach([&](SlotIndex slot, const Order& order) {
            func(order);
        });
    }
    
    // Invariant checking
    bool checkInvariants(std::string& error) const;
    
    // Get order pool for testing
    const ObjectPool<Order>& pool() const { return orderPool_; }
    
    // Get price level
    const PriceLevel& getBidLevel(PriceOffset offset) const { return bidLevels_[offset]; }
    const PriceLevel& getAskLevel(PriceOffset offset) const { return askLevels_[offset]; }
    
    // Total quantity at all bid/ask levels
    Quantity totalBidQuantity() const;
    Quantity totalAskQuantity() const;
    
    // State digest
    std::uint64_t computeDigest() const;

private:
    // Convert price to offset in level array
    PriceOffset priceToOffset(Price price) const {
        return static_cast<PriceOffset>((price - config_.minPrice) / config_.tickSize);
    }
    
    // Convert offset back to price
    Price offsetToPrice(PriceOffset offset) const {
        return config_.minPrice + static_cast<Price>(offset) * config_.tickSize;
    }
    
    // Check if price is valid
    bool isValidPrice(Price price) const {
        return price >= config_.minPrice && 
               price <= config_.maxPrice &&
               (price - config_.minPrice) % config_.tickSize == 0;
    }
    
    // Find best price (bid: highest, ask: lowest)
    PriceOffset findBestBid() const;
    PriceOffset findBestAsk() const;
    
    // Order queue operations
    void enqueueOrder(SlotIndex orderSlot, PriceLevel& level);
    SlotIndex dequeueOrder(PriceLevel& level);
    void removeFromQueue(SlotIndex orderSlot, PriceLevel& level);
    
    // Matching
    Result matchOrder(Order& aggressor);
    
    // Self-trade prevention
    bool wouldSelfTrade(const Order& aggressor, const Order& passive) const;
    
    // Update level after order change
    void updateLevelAfterAdd(PriceLevel& level, const Order& order);
    void updateLevelAfterRemove(PriceLevel& level, const Order& order);
    void updateLevelAfterFill(PriceLevel& level, const Order& order, Quantity fillQty);
    
private:
    Config config_;
    std::uint32_t numPriceLevels_;
    
    // Order storage
    ObjectPool<Order> orderPool_;
    FixedRobinHoodMap<OrderKey, SlotIndex> orderIndex_;
    
    // Price levels (preallocated)
    std::vector<PriceLevel> bidLevels_;
    std::vector<PriceLevel> askLevels_;
    
    // Occupancy bitsets for finding best bid/ask
    std::vector<std::uint64_t> bidOccupancy_;
    std::vector<std::uint64_t> askOccupancy_;
    
    // Tracking
    PriceOffset bestBidOffset_ = INVALID_PRICE_OFFSET;
    PriceOffset bestAskOffset_ = INVALID_PRICE_OFFSET;
    
    // Match ID counter
    std::uint64_t nextMatchId_ = 1;
};

} // namespace lockstep
