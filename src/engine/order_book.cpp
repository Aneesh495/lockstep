#include "lockstep/engine/order_book.hpp"
#include <algorithm>
#include <cassert>

namespace lockstep {

namespace {
    // Round up to next power of 2
    std::uint32_t nextPowerOf2(std::uint32_t n) {
        if (n == 0) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        return n + 1;
    }
}

OrderBook::OrderBook(const Config& config)
    : config_(config)
    , numPriceLevels_(static_cast<std::uint32_t>((config.maxPrice - config.minPrice) / config.tickSize + 1))
    , orderPool_(config.maxOrders)
    , orderIndex_(nextPowerOf2(config.maxOrders * 2)) // Lower load factor, power of 2
    , bidLevels_(numPriceLevels_)
    , askLevels_(numPriceLevels_)
{
    // Initialize price levels
    for (PriceOffset i = 0; i < numPriceLevels_; ++i) {
        bidLevels_[i].price = offsetToPrice(i);
        bidLevels_[i].priceOffset = i;
        askLevels_[i].price = offsetToPrice(i);
        askLevels_[i].priceOffset = i;
    }
    
    // Initialize occupancy bitsets
    std::uint32_t numWords = (numPriceLevels_ + 63) / 64;
    bidOccupancy_.resize(numWords, 0);
    askOccupancy_.resize(numWords, 0);
}

OrderBook::Result OrderBook::newOrder(Order& order) {
    Result result;
    
    // Validate
    if (!isValidPrice(order.price)) {
        result.reason = RejectionReason::InvalidPrice;
        return result;
    }
    
    if (order.quantity == 0) {
        result.reason = RejectionReason::InvalidQuantity;
        return result;
    }
    
    // Check for duplicate
    OrderKey key{order.clientId, order.orderId};
    if (orderIndex_.contains(key)) {
        result.reason = RejectionReason::DuplicateOrderId;
        return result;
    }
    
    // Check capacity
    if (orderPool_.full()) {
        result.reason = RejectionReason::CapacityExceeded;
        return result;
    }
    
    // Check if marketable (crosses the spread)
    if (order.side == Side::Buy && bestAskOffset_ != INVALID_PRICE_OFFSET) {
        Price bestAskPrice = offsetToPrice(bestAskOffset_);
        if (order.price >= bestAskPrice) {
            // Marketable buy
            result = matchOrder(order);
            if (order.tif == TimeInForce::FOK && result.filledQuantity < order.quantity) {
                // FOK failed - no mutation
                result.success = false;
                result.reason = RejectionReason::FOKCannotFill;
                result.matches.clear();
                result.filledQuantity = 0;
                return result;
            }
            if (order.remainingQuantity() == 0 || order.tif == TimeInForce::IOC) {
                // Fully filled or IOC
                result.success = true;
                return result;
            }
        }
    } else if (order.side == Side::Sell && bestBidOffset_ != INVALID_PRICE_OFFSET) {
        Price bestBidPrice = offsetToPrice(bestBidOffset_);
        if (order.price <= bestBidPrice) {
            // Marketable sell
            result = matchOrder(order);
            if (order.tif == TimeInForce::FOK && result.filledQuantity < order.quantity) {
                result.success = false;
                result.reason = RejectionReason::FOKCannotFill;
                result.matches.clear();
                result.filledQuantity = 0;
                return result;
            }
            if (order.remainingQuantity() == 0 || order.tif == TimeInForce::IOC) {
                result.success = true;
                return result;
            }
        }
    }
    
    // Rest the order
    SlotIndex slot = orderPool_.allocate();
    if (slot == INVALID_SLOT) {
        result.reason = RejectionReason::CapacityExceeded;
        return result;
    }
    
    Order& newOrder = orderPool_[slot];
    newOrder = order;
    newOrder.status = OrderStatus::Live;
    newOrder.executedQuantity = result.filledQuantity;
    
    PriceOffset offset = priceToOffset(order.price);
    newOrder.priceLevelOffset = offset;
    
    // Add to price level
    PriceLevel& level = (order.side == Side::Buy) ? bidLevels_[offset] : askLevels_[offset];
    enqueueOrder(slot, level);
    
    // Update occupancy bitset
    std::uint32_t wordIdx = offset / 64;
    std::uint32_t bitIdx = offset % 64;
    if (order.side == Side::Buy) {
        bidOccupancy_[wordIdx] |= (1ULL << bitIdx);
        if (bestBidOffset_ == INVALID_PRICE_OFFSET || offset > bestBidOffset_) {
            bestBidOffset_ = offset;
        }
    } else {
        askOccupancy_[wordIdx] |= (1ULL << bitIdx);
        if (bestAskOffset_ == INVALID_PRICE_OFFSET || offset < bestAskOffset_) {
            bestAskOffset_ = offset;
        }
    }
    
    // Add to index
    orderIndex_.insert(key, slot);
    
    result.success = true;
    return result;
}

OrderBook::Result OrderBook::cancelOrder(ClientId clientId, OrderId orderId) {
    Result result;
    
    OrderKey key{clientId, orderId};
    auto slotOpt = orderIndex_.find(key);
    if (!slotOpt) {
        result.reason = RejectionReason::OrderNotFound;
        return result;
    }
    
    SlotIndex slot = *slotOpt;
    Order& order = orderPool_[slot];
    
    if (order.isTerminal()) {
        result.reason = RejectionReason::OrderNotLive;
        return result;
    }
    
    // Remove from price level
    PriceOffset offset = order.priceLevelOffset;
    PriceLevel& level = (order.side == Side::Buy) ? bidLevels_[offset] : askLevels_[offset];
    removeFromQueue(slot, level);
    
    // Update occupancy bitset if level is empty
    if (level.empty()) {
        std::uint32_t wordIdx = offset / 64;
        std::uint32_t bitIdx = offset % 64;
        if (order.side == Side::Buy) {
            bidOccupancy_[wordIdx] &= ~(1ULL << bitIdx);
            if (bestBidOffset_ == offset) {
                bestBidOffset_ = findBestBid();
            }
        } else {
            askOccupancy_[wordIdx] &= ~(1ULL << bitIdx);
            if (bestAskOffset_ == offset) {
                bestAskOffset_ = findBestAsk();
            }
        }
    }
    
    // Mark as canceled
    order.status = OrderStatus::Canceled;
    
    // Remove from index
    orderIndex_.erase(key);
    
    // Return slot to pool
    orderPool_.deallocate(slot);
    
    result.success = true;
    return result;
}

OrderBook::Result OrderBook::replaceOrder(ClientId clientId, OrderId oldOrderId, OrderId newOrderId,
                                          Price newPrice, Quantity newQuantity) {
    Result result;
    
    // Find existing order
    OrderKey oldKey{clientId, oldOrderId};
    auto slotOpt = orderIndex_.find(oldKey);
    if (!slotOpt) {
        result.reason = RejectionReason::OrderNotFound;
        return result;
    }
    
    SlotIndex slot = *slotOpt;
    Order& oldOrder = orderPool_[slot];
    
    if (oldOrder.isTerminal()) {
        result.reason = RejectionReason::OrderNotLive;
        return result;
    }
    
    // Validate new price
    if (!isValidPrice(newPrice)) {
        result.reason = RejectionReason::InvalidPrice;
        return result;
    }
    
    if (newQuantity == 0) {
        result.reason = RejectionReason::InvalidQuantity;
        return result;
    }
    
    // Check for duplicate new order ID
    OrderKey newKey{clientId, newOrderId};
    if (newOrderId != oldOrderId && orderIndex_.contains(newKey)) {
        result.reason = RejectionReason::DuplicateOrderId;
        return result;
    }
    
    // Determine if priority is lost
    bool losesPriority = (newPrice != oldOrder.price) || 
                         (newQuantity > oldOrder.remainingQuantity());
    
    // Remove from old price level
    PriceOffset oldOffset = oldOrder.priceLevelOffset;
    PriceLevel& oldLevel = (oldOrder.side == Side::Buy) ? bidLevels_[oldOffset] : askLevels_[oldOffset];
    removeFromQueue(slot, oldLevel);
    
    // Update occupancy if level is empty
    if (oldLevel.empty()) {
        std::uint32_t wordIdx = oldOffset / 64;
        std::uint32_t bitIdx = oldOffset % 64;
        if (oldOrder.side == Side::Buy) {
            bidOccupancy_[wordIdx] &= ~(1ULL << bitIdx);
            if (bestBidOffset_ == oldOffset) {
                bestBidOffset_ = findBestBid();
            }
        } else {
            askOccupancy_[wordIdx] &= ~(1ULL << bitIdx);
            if (bestAskOffset_ == oldOffset) {
                bestAskOffset_ = findBestAsk();
            }
        }
    }
    
    // Update order
    oldOrder.orderId = newOrderId;
    oldOrder.price = newPrice;
    oldOrder.quantity = oldOrder.executedQuantity + newQuantity;
    
    // Add to new price level
    PriceOffset newOffset = priceToOffset(newPrice);
    oldOrder.priceLevelOffset = newOffset;
    
    PriceLevel& newLevel = (oldOrder.side == Side::Buy) ? bidLevels_[newOffset] : askLevels_[newOffset];
    
    if (losesPriority) {
        // Add to back of queue (loses priority)
        enqueueOrder(slot, newLevel);
    } else {
        // Maintain position - for simplicity, we still add to back
        // A more complex implementation would preserve exact position
        enqueueOrder(slot, newLevel);
    }
    
    // Update occupancy bitset
    std::uint32_t wordIdx = newOffset / 64;
    std::uint32_t bitIdx = newOffset % 64;
    if (oldOrder.side == Side::Buy) {
        bidOccupancy_[wordIdx] |= (1ULL << bitIdx);
        if (bestBidOffset_ == INVALID_PRICE_OFFSET || newOffset > bestBidOffset_) {
            bestBidOffset_ = newOffset;
        }
    } else {
        askOccupancy_[wordIdx] |= (1ULL << bitIdx);
        if (bestAskOffset_ == INVALID_PRICE_OFFSET || newOffset < bestAskOffset_) {
            bestAskOffset_ = newOffset;
        }
    }
    
    // Update index
    if (newOrderId != oldOrderId) {
        orderIndex_.erase(oldKey);
        orderIndex_.insert(newKey, slot);
    }
    
    result.success = true;
    return result;
}

std::uint32_t OrderBook::massCancel(ClientId clientId) {
    std::uint32_t canceled = 0;
    
    std::vector<OrderKey> toCancel;
    orderIndex_.forEach([&](const OrderKey& key, SlotIndex slot) {
        if (key.clientId == clientId) {
            toCancel.push_back(key);
        }
    });
    
    for (const auto& key : toCancel) {
        auto result = cancelOrder(key.clientId, key.orderId);
        if (result.success) {
            ++canceled;
        }
    }
    
    return canceled;
}

std::optional<std::reference_wrapper<Order>> OrderBook::findOrder(ClientId clientId, OrderId orderId) {
    OrderKey key{clientId, orderId};
    auto slotOpt = orderIndex_.find(key);
    if (!slotOpt) {
        return std::nullopt;
    }
    return std::ref(orderPool_[*slotOpt]);
}

std::optional<std::reference_wrapper<const Order>> OrderBook::findOrder(ClientId clientId, OrderId orderId) const {
    OrderKey key{clientId, orderId};
    auto slotOpt = orderIndex_.find(key);
    if (!slotOpt) {
        return std::nullopt;
    }
    return std::cref(orderPool_[*slotOpt]);
}

Price OrderBook::bestBid() const {
    if (bestBidOffset_ == INVALID_PRICE_OFFSET) {
        return 0;
    }
    return offsetToPrice(bestBidOffset_);
}

Price OrderBook::bestAsk() const {
    if (bestAskOffset_ == INVALID_PRICE_OFFSET) {
        return 0;
    }
    return offsetToPrice(bestAskOffset_);
}

Quantity OrderBook::bidQuantity(Price price) const {
    if (!isValidPrice(price)) return 0;
    PriceOffset offset = priceToOffset(price);
    return bidLevels_[offset].totalQuantity;
}

Quantity OrderBook::askQuantity(Price price) const {
    if (!isValidPrice(price)) return 0;
    PriceOffset offset = priceToOffset(price);
    return askLevels_[offset].totalQuantity;
}

Quantity OrderBook::totalBidQuantity() const {
    Quantity total = 0;
    for (const auto& level : bidLevels_) {
        total += level.totalQuantity;
    }
    return total;
}

Quantity OrderBook::totalAskQuantity() const {
    Quantity total = 0;
    for (const auto& level : askLevels_) {
        total += level.totalQuantity;
    }
    return total;
}

PriceOffset OrderBook::findBestBid() const {
    // Scan from highest to lowest
    for (int wordIdx = static_cast<int>(bidOccupancy_.size()) - 1; wordIdx >= 0; --wordIdx) {
        std::uint64_t word = bidOccupancy_[static_cast<std::size_t>(wordIdx)];
        if (word == 0) continue;
        
        // Find highest set bit
        int bitIdx = 63 - __builtin_clzll(word);
        return static_cast<PriceOffset>(static_cast<std::uint32_t>(wordIdx) * 64 + static_cast<std::uint32_t>(bitIdx));
    }
    return INVALID_PRICE_OFFSET;
}

PriceOffset OrderBook::findBestAsk() const {
    // Scan from lowest to highest
    for (std::size_t wordIdx = 0; wordIdx < askOccupancy_.size(); ++wordIdx) {
        std::uint64_t word = askOccupancy_[wordIdx];
        if (word == 0) continue;
        
        // Find lowest set bit
        int bitIdx = __builtin_ctzll(word);
        return static_cast<PriceOffset>(wordIdx * 64 + static_cast<std::uint32_t>(bitIdx));
    }
    return INVALID_PRICE_OFFSET;
}

void OrderBook::enqueueOrder(SlotIndex orderSlot, PriceLevel& level) {
    Order& order = orderPool_[orderSlot];
    order.prev = level.tail;
    order.next = INVALID_SLOT;
    
    if (level.tail != INVALID_SLOT) {
        orderPool_[level.tail].next = orderSlot;
    } else {
        level.head = orderSlot;
    }
    level.tail = orderSlot;
    
    level.orderCount++;
    level.totalQuantity += order.remainingQuantity();
}

SlotIndex OrderBook::dequeueOrder(PriceLevel& level) {
    if (level.head == INVALID_SLOT) {
        return INVALID_SLOT;
    }
    
    SlotIndex slot = level.head;
    Order& order = orderPool_[slot];
    
    level.head = order.next;
    if (level.head != INVALID_SLOT) {
        orderPool_[level.head].prev = INVALID_SLOT;
    } else {
        level.tail = INVALID_SLOT;
    }
    
    level.orderCount--;
    level.totalQuantity -= order.remainingQuantity();
    
    return slot;
}

void OrderBook::removeFromQueue(SlotIndex orderSlot, PriceLevel& level) {
    Order& order = orderPool_[orderSlot];
    
    if (order.prev != INVALID_SLOT) {
        orderPool_[order.prev].next = order.next;
    } else {
        level.head = order.next;
    }
    
    if (order.next != INVALID_SLOT) {
        orderPool_[order.next].prev = order.prev;
    } else {
        level.tail = order.prev;
    }
    
    level.orderCount--;
    level.totalQuantity -= order.remainingQuantity();
}

OrderBook::Result OrderBook::matchOrder(Order& aggressor) {
    Result result;
    
    while (aggressor.remainingQuantity() > 0) {
        // Find best opposing side
        PriceOffset bestOffset = (aggressor.side == Side::Buy) ? bestAskOffset_ : bestBidOffset_;
        
        if (bestOffset == INVALID_PRICE_OFFSET) {
            break; // No more liquidity
        }
        
        Price bestPrice = offsetToPrice(bestOffset);
        
        // Check if aggressor can trade at this price
        if (aggressor.side == Side::Buy && aggressor.price < bestPrice) {
            break;
        }
        if (aggressor.side == Side::Sell && aggressor.price > bestPrice) {
            break;
        }
        
        // Match against orders at this level
        PriceLevel& level = (aggressor.side == Side::Buy) ? askLevels_[bestOffset] : bidLevels_[bestOffset];
        
        bool selfTrade = false;
        while (aggressor.remainingQuantity() > 0 && level.head != INVALID_SLOT) {
            SlotIndex passiveSlot = level.head;
            Order& passive = orderPool_[passiveSlot];
            
            // Self-trade prevention
            if (wouldSelfTrade(aggressor, passive)) {
                selfTrade = true;
                break;
            }
            
            // Calculate fill quantity
            Quantity fillQty = std::min(aggressor.remainingQuantity(), passive.remainingQuantity());
            
            // Create match
            Match match;
            match.matchId = nextMatchId_++;
            match.passiveOrderId = passive.orderId;
            match.aggressiveOrderId = aggressor.orderId;
            match.price = bestPrice;
            match.quantity = fillQty;
            match.aggressiveLiquidity = Liquidity::Aggressive;
            
            result.matches.push_back(match);
            result.filledQuantity += fillQty;
            
            // Update orders
            aggressor.executedQuantity += fillQty;
            passive.executedQuantity += fillQty;
            
            // Update level
            level.totalQuantity -= fillQty;
            
            // Remove passive if fully filled
            if (passive.remainingQuantity() == 0) {
                OrderKey passiveKey{passive.clientId, passive.orderId};
                orderIndex_.erase(passiveKey);
                
                dequeueOrder(level);
                passive.status = OrderStatus::Filled;
                orderPool_.deallocate(passiveSlot);
            }
        }
        
        // Exit outer loop on self-trade
        if (selfTrade) {
            break;
        }
        
        // Update occupancy if level is empty
        if (level.empty()) {
            std::uint32_t wordIdx = bestOffset / 64;
            std::uint32_t bitIdx = bestOffset % 64;
            
            if (aggressor.side == Side::Buy) {
                askOccupancy_[wordIdx] &= ~(1ULL << bitIdx);
                bestAskOffset_ = findBestAsk();
            } else {
                bidOccupancy_[wordIdx] &= ~(1ULL << bitIdx);
                bestBidOffset_ = findBestBid();
            }
        }
    }
    
    result.success = true;
    return result;
}

bool OrderBook::wouldSelfTrade(const Order& aggressor, const Order& passive) const {
    return aggressor.clientId == passive.clientId;
}

bool OrderBook::checkInvariants(std::string& error) const {
    // Check book is not crossed
    if (bestBidOffset_ != INVALID_PRICE_OFFSET && bestAskOffset_ != INVALID_PRICE_OFFSET) {
        Price bid = offsetToPrice(bestBidOffset_);
        Price ask = offsetToPrice(bestAskOffset_);
        if (bid >= ask) {
            error = "Book is crossed: bid=" + std::to_string(bid) + " >= ask=" + std::to_string(ask);
            return false;
        }
    }
    
    // Check order count matches pool size
    std::uint32_t liveOrders = 0;
    orderPool_.forEach([&](SlotIndex, const Order& order) {
        if (order.isActive()) {
            ++liveOrders;
        }
    });
    
    // Check each order exists in index
    std::uint32_t indexedOrders = 0;
    orderIndex_.forEach([&](const OrderKey&, SlotIndex slot) {
        if (orderPool_.isAllocated(slot)) {
            ++indexedOrders;
        }
    });
    
    if (liveOrders != indexedOrders) {
        error = "Order count mismatch: live=" + std::to_string(liveOrders) + 
                " indexed=" + std::to_string(indexedOrders);
        return false;
    }
    
    return true;
}

std::uint64_t OrderBook::computeDigest() const {
    std::uint64_t digest = 0;
    
    // Hash all orders in deterministic order (by price, then by time)
    std::vector<std::pair<Price, SlotIndex>> orders;
    orderPool_.forEach([&](SlotIndex slot, const Order& order) {
        if (order.isActive()) {
            orders.push_back({order.price, slot});
        }
    });
    
    std::sort(orders.begin(), orders.end());
    
    for (const auto& [price, slot] : orders) {
        const Order& order = orderPool_[slot];
        std::uint64_t h = static_cast<std::uint64_t>(price);
        h ^= static_cast<std::uint64_t>(order.orderId) * 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<std::uint64_t>(order.quantity) * 0xbf58476d1ce4e5b9ULL;
        h ^= static_cast<std::uint64_t>(order.executedQuantity) * 0x94d049bb133111ebULL;
        digest ^= h;
    }
    
    return digest;
}

} // namespace lockstep
