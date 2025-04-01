#include "lockstep/engine/reference_book.hpp"

namespace lockstep {

ReferenceBook::Result ReferenceBook::newOrder(Order& order) {
    Result result;
    
    // Check for duplicate
    auto key = std::make_pair(order.clientId, order.orderId);
    if (orders_.count(key)) {
        result.reason = RejectionReason::DuplicateOrderId;
        return result;
    }
    
    if (order.quantity == 0) {
        result.reason = RejectionReason::InvalidQuantity;
        return result;
    }
    
    // Try to match
    result = matchOrder(order);
    
    if (order.tif == TimeInForce::FOK && result.filledQuantity < order.quantity) {
        return Result{false, RejectionReason::FOKCannotFill, 0, {}};
    }
    
    if (order.remainingQuantity() == 0 || order.tif == TimeInForce::IOC) {
        return Result{true, RejectionReason::None, result.filledQuantity, result.matches};
    }
    
    // Rest the order
    order.status = OrderStatus::Live;
    order.executedQuantity = result.filledQuantity;
    
    if (order.side == Side::Buy) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
    
    orders_[key] = order;
    result.success = true;
    return result;
}

ReferenceBook::Result ReferenceBook::cancelOrder(ClientId clientId, OrderId orderId) {
    Result result;
    
    auto key = std::make_pair(clientId, orderId);
    auto it = orders_.find(key);
    if (it == orders_.end()) {
        result.reason = RejectionReason::OrderNotFound;
        return result;
    }
    
    Order& order = it->second;
    if (order.isTerminal()) {
        result.reason = RejectionReason::OrderNotLive;
        return result;
    }
    
    // Remove from book
    if (order.side == Side::Buy) {
        auto& queue = bids_[order.price];
        queue.erase(std::remove_if(queue.begin(), queue.end(),
            [&](const Order& o) { return o.orderId == orderId; }), queue.end());
        if (queue.empty()) {
            bids_.erase(order.price);
        }
    } else {
        auto& queue = asks_[order.price];
        queue.erase(std::remove_if(queue.begin(), queue.end(),
            [&](const Order& o) { return o.orderId == orderId; }), queue.end());
        if (queue.empty()) {
            asks_.erase(order.price);
        }
    }
    
    orders_.erase(it);
    result.success = true;
    return result;
}

ReferenceBook::Result ReferenceBook::replaceOrder(ClientId clientId, OrderId oldOrderId,
                                                   OrderId newOrderId, Price newPrice, Quantity newQuantity) {
    Result result;
    
    auto oldKey = std::make_pair(clientId, oldOrderId);
    auto it = orders_.find(oldKey);
    if (it == orders_.end()) {
        result.reason = RejectionReason::OrderNotFound;
        return result;
    }
    
    Order order = it->second;
    if (order.isTerminal()) {
        result.reason = RejectionReason::OrderNotLive;
        return result;
    }
    
    // Cancel old
    auto cancelResult = cancelOrder(clientId, oldOrderId);
    if (!cancelResult.success) {
        return cancelResult;
    }
    
    // Create new
    order.orderId = newOrderId;
    order.price = newPrice;
    order.quantity = order.executedQuantity + newQuantity;
    
    return newOrder(order);
}

std::uint32_t ReferenceBook::massCancel(ClientId clientId) {
    std::uint32_t canceled = 0;
    std::vector<std::pair<ClientId, OrderId>> toCancel;
    
    for (const auto& [key, order] : orders_) {
        if (key.first == clientId && order.isActive()) {
            toCancel.push_back(key);
        }
    }
    
    for (const auto& key : toCancel) {
        auto result = cancelOrder(key.first, key.second);
        if (result.success) {
            ++canceled;
        }
    }
    
    return canceled;
}

std::optional<Order> ReferenceBook::findOrder(ClientId clientId, OrderId orderId) const {
    auto key = std::make_pair(clientId, orderId);
    auto it = orders_.find(key);
    if (it == orders_.end()) {
        return std::nullopt;
    }
    return it->second;
}

Price ReferenceBook::bestBid() const {
    if (bids_.empty()) return 0;
    return bids_.begin()->first;
}

Price ReferenceBook::bestAsk() const {
    if (asks_.empty()) return 0;
    return asks_.begin()->first;
}

Quantity ReferenceBook::bidQuantity(Price price) const {
    auto it = bids_.find(price);
    if (it == bids_.end()) return 0;
    Quantity qty = 0;
    for (const auto& order : it->second) {
        qty += order.remainingQuantity();
    }
    return qty;
}

Quantity ReferenceBook::askQuantity(Price price) const {
    auto it = asks_.find(price);
    if (it == asks_.end()) return 0;
    Quantity qty = 0;
    for (const auto& order : it->second) {
        qty += order.remainingQuantity();
    }
    return qty;
}

Quantity ReferenceBook::totalBidQuantity() const {
    Quantity total = 0;
    for (const auto& [price, queue] : bids_) {
        for (const auto& order : queue) {
            total += order.remainingQuantity();
        }
    }
    return total;
}

Quantity ReferenceBook::totalAskQuantity() const {
    Quantity total = 0;
    for (const auto& [price, queue] : asks_) {
        for (const auto& order : queue) {
            total += order.remainingQuantity();
        }
    }
    return total;
}

ReferenceBook::Result ReferenceBook::matchOrder(Order& aggressor) {
    Result result;
    
    while (aggressor.remainingQuantity() > 0) {
        if (aggressor.side == Side::Buy) {
            // Matching against asks
            if (asks_.empty()) break;
            
            Price bestPrice = asks_.begin()->first;
            if (aggressor.price < bestPrice) break;
            
            auto& queue = asks_.begin()->second;
            
            bool selfTrade = false;
            while (aggressor.remainingQuantity() > 0 && !queue.empty()) {
                Order& passive = queue.front();
                
                // Self-trade prevention
                if (passive.clientId == aggressor.clientId) {
                    selfTrade = true;
                    break;
                }
                
                Quantity fillQty = std::min(aggressor.remainingQuantity(), passive.remainingQuantity());
                
                Match match;
                match.matchId = nextMatchId_++;
                match.passiveOrderId = passive.orderId;
                match.aggressiveOrderId = aggressor.orderId;
                match.price = bestPrice;
                match.quantity = fillQty;
                
                result.matches.push_back(match);
                result.filledQuantity += fillQty;
                
                aggressor.executedQuantity += fillQty;
                passive.executedQuantity += fillQty;
                
                // Update order in lookup
                orders_[std::make_pair(passive.clientId, passive.orderId)] = passive;
                
                if (passive.remainingQuantity() == 0) {
                    orders_.erase(std::make_pair(passive.clientId, passive.orderId));
                    queue.pop_front();
                }
            }
            
            if (queue.empty()) {
                asks_.erase(asks_.begin());
            }
            
            if (selfTrade) break;
        } else {
            // Matching against bids
            if (bids_.empty()) break;
            
            Price bestPrice = bids_.begin()->first;
            if (aggressor.price > bestPrice) break;
            
            auto& queue = bids_.begin()->second;
            
            bool selfTrade = false;
            while (aggressor.remainingQuantity() > 0 && !queue.empty()) {
                Order& passive = queue.front();
                
                // Self-trade prevention
                if (passive.clientId == aggressor.clientId) {
                    selfTrade = true;
                    break;
                }
                
                Quantity fillQty = std::min(aggressor.remainingQuantity(), passive.remainingQuantity());
                
                Match match;
                match.matchId = nextMatchId_++;
                match.passiveOrderId = passive.orderId;
                match.aggressiveOrderId = aggressor.orderId;
                match.price = bestPrice;
                match.quantity = fillQty;
                
                result.matches.push_back(match);
                result.filledQuantity += fillQty;
                
                aggressor.executedQuantity += fillQty;
                passive.executedQuantity += fillQty;
                
                // Update order in lookup
                orders_[std::make_pair(passive.clientId, passive.orderId)] = passive;
                
                if (passive.remainingQuantity() == 0) {
                    orders_.erase(std::make_pair(passive.clientId, passive.orderId));
                    queue.pop_front();
                }
            }
            
            if (queue.empty()) {
                bids_.erase(bids_.begin());
            }
            
            if (selfTrade) break;
        }
    }
    
    result.success = true;
    return result;
}

std::uint64_t ReferenceBook::computeDigest() const {
    std::uint64_t digest = 0;
    
    std::vector<std::tuple<Price, OrderId, Quantity, Quantity>> entries;
    for (const auto& [key, order] : orders_) {
        if (order.isActive()) {
            entries.push_back({order.price, order.orderId, order.quantity, order.executedQuantity});
        }
    }
    
    std::sort(entries.begin(), entries.end());
    
    for (const auto& [price, orderId, qty, execQty] : entries) {
        std::uint64_t h = static_cast<std::uint64_t>(price);
        h ^= orderId * 0x9e3779b97f4a7c15ULL;
        h ^= static_cast<std::uint64_t>(qty) * 0xbf58476d1ce4e5b9ULL;
        h ^= static_cast<std::uint64_t>(execQty) * 0x94d049bb133111ebULL;
        digest ^= h;
    }
    
    return digest;
}

} // namespace lockstep
