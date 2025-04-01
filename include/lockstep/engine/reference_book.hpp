#pragma once

#include <cstdint>
#include <map>
#include <deque>
#include <optional>
#include "lockstep/common/types.hpp"
#include "lockstep/engine/order.hpp"

namespace lockstep {

// Reference order book using std::map and std::deque
// Used for differential testing against optimized book
// Simple, correct implementation for verification

class ReferenceBook {
public:
    struct Result {
        bool success = false;
        RejectionReason reason = RejectionReason::None;
        Quantity filledQuantity = 0;
        std::vector<Match> matches;
    };
    
    Result newOrder(Order& order);
    Result cancelOrder(ClientId clientId, OrderId orderId);
    Result replaceOrder(ClientId clientId, OrderId oldOrderId, OrderId newOrderId,
                       Price newPrice, Quantity newQuantity);
    std::uint32_t massCancel(ClientId clientId);
    
    std::optional<Order> findOrder(ClientId clientId, OrderId orderId) const;
    
    Price bestBid() const;
    Price bestAsk() const;
    Quantity bidQuantity(Price price) const;
    Quantity askQuantity(Price price) const;
    
    std::uint32_t orderCount() const { return static_cast<std::uint32_t>(orders_.size()); }
    bool empty() const { return orders_.empty(); }
    
    // For differential comparison
    template <typename Func>
    void forEachOrder(Func&& func) const {
        for (const auto& [key, order] : orders_) {
            func(order);
        }
    }
    
    Quantity totalBidQuantity() const;
    Quantity totalAskQuantity() const;
    
    std::uint64_t computeDigest() const;

private:
    // Price -> deque of orders (FIFO)
    using OrderQueue = std::deque<Order>;
    using BidBook = std::map<Price, OrderQueue, std::greater<Price>>; // Highest first
    using AskBook = std::map<Price, OrderQueue>; // Lowest first
    
    // Order lookup
    std::map<std::pair<ClientId, OrderId>, Order> orders_;
    
    // Books
    BidBook bids_;
    AskBook asks_;
    
    // Match ID counter
    std::uint64_t nextMatchId_ = 1;
    
    Result matchOrder(Order& aggressor);
};

} // namespace lockstep
