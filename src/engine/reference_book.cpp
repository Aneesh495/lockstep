#include "lockstep/engine/reference_book.hpp"
#include <algorithm>

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

    // Preflight FOK orders: must be able to fill completely without self-trading
    if (order.tif == TimeInForce::FOK && !canFillFok(order)) {
        return Result{false, RejectionReason::FOKCannotFill, 0, {}};
    }

    // Try to match
    result = matchOrder(order);

    if (order.tif == TimeInForce::FOK && result.filledQuantity < order.quantity) {
        return Result{false, RejectionReason::FOKCannotFill, 0, {}};
    }

    if (order.remainingQuantity() == 0 || order.tif == TimeInForce::IOC) {
        return Result{true, RejectionReason::None, result.filledQuantity, result.matches};
    }

    if (order.tif == TimeInForce::FOK) {
        return Result{false, RejectionReason::FOKCannotFill, 0, {}};
    }

    // Check if remaining order would cross opposing book (stopped by self-trade prevention)
    if (order.side == Side::Buy && !asks_.empty() && order.price >= asks_.begin()->first) {
        if (result.filledQuantity == 0) {
            return Result{false, RejectionReason::SelfTradePrevention, 0, {}};
        }
        return Result{true, RejectionReason::None, result.filledQuantity, result.matches};
    }
    if (order.side == Side::Sell && !bids_.empty() && order.price <= bids_.begin()->first) {
        if (result.filledQuantity == 0) {
            return Result{false, RejectionReason::SelfTradePrevention, 0, {}};
        }
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
                                   [&](const Order& o) {
                                       return o.orderId == orderId && o.clientId == clientId;
                                   }),
                    queue.end());
        if (queue.empty()) {
            bids_.erase(order.price);
        }
    } else {
        auto& queue = asks_[order.price];
        queue.erase(std::remove_if(queue.begin(), queue.end(),
                                   [&](const Order& o) {
                                       return o.orderId == orderId && o.clientId == clientId;
                                   }),
                    queue.end());
        if (queue.empty()) {
            asks_.erase(order.price);
        }
    }

    orders_.erase(it);
    result.success = true;
    return result;
}

ReferenceBook::Result ReferenceBook::replaceOrder(ClientId clientId, OrderId oldOrderId,
                                                  OrderId newOrderId, Price newPrice,
                                                  Quantity newQuantity) {
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

    if (orders_.contains({clientId, newOrderId}) && newOrderId != oldOrderId) {
        result.reason = RejectionReason::DuplicateOrderId;
        return result;
    }
    if (newQuantity == 0) {
        result.reason = RejectionReason::InvalidQuantity;
        return result;
    }
    if (newPrice == order.price && newQuantity <= order.remainingQuantity()) {
        auto adjust = [&](auto& levels) {
            for (auto& o : levels.at(order.price)) {
                if (o.clientId == clientId && o.orderId == oldOrderId) {
                    o.quantity = o.executedQuantity + newQuantity;
                    o.orderId = newOrderId;
                    orders_.erase(oldKey);
                    orders_[{clientId, newOrderId}] = o;
                    break;
                }
            }
        };
        if (order.side == Side::Buy)
            adjust(bids_);
        else
            adjust(asks_);
        result.success = true;
        return result;
    }
    cancelOrder(clientId, oldOrderId);
    order.orderId = newOrderId;
    order.price = newPrice;
    order.quantity = newQuantity;
    order.executedQuantity = 0;
    order.tif = TimeInForce::GTC;
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
    if (bids_.empty())
        return 0;
    return bids_.begin()->first;
}

Price ReferenceBook::bestAsk() const {
    if (asks_.empty())
        return 0;
    return asks_.begin()->first;
}

Quantity ReferenceBook::bidQuantity(Price price) const {
    auto it = bids_.find(price);
    if (it == bids_.end())
        return 0;
    Quantity qty = 0;
    for (const auto& order : it->second) {
        qty += order.remainingQuantity();
    }
    return qty;
}

Quantity ReferenceBook::askQuantity(Price price) const {
    auto it = asks_.find(price);
    if (it == asks_.end())
        return 0;
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
            if (asks_.empty())
                break;

            Price bestPrice = asks_.begin()->first;
            if (aggressor.price < bestPrice)
                break;

            auto& queue = asks_.begin()->second;

            bool selfTrade = false;
            while (aggressor.remainingQuantity() > 0 && !queue.empty()) {
                Order& passive = queue.front();

                // Self-trade prevention
                if (passive.clientId == aggressor.clientId) {
                    selfTrade = true;
                    break;
                }

                Quantity fillQty =
                    std::min(aggressor.remainingQuantity(), passive.remainingQuantity());

                Match match;
                match.matchId = nextMatchId_++;
                match.passiveOrderId = passive.orderId;
                match.aggressiveOrderId = aggressor.orderId;
                match.passiveClientId = passive.clientId;
                match.aggressiveClientId = aggressor.clientId;
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

            if (selfTrade)
                break;
        } else {
            // Matching against bids
            if (bids_.empty())
                break;

            Price bestPrice = bids_.begin()->first;
            if (aggressor.price > bestPrice)
                break;

            auto& queue = bids_.begin()->second;

            bool selfTrade = false;
            while (aggressor.remainingQuantity() > 0 && !queue.empty()) {
                Order& passive = queue.front();

                // Self-trade prevention
                if (passive.clientId == aggressor.clientId) {
                    selfTrade = true;
                    break;
                }

                Quantity fillQty =
                    std::min(aggressor.remainingQuantity(), passive.remainingQuantity());

                Match match;
                match.matchId = nextMatchId_++;
                match.passiveOrderId = passive.orderId;
                match.aggressiveOrderId = aggressor.orderId;
                match.passiveClientId = passive.clientId;
                match.aggressiveClientId = aggressor.clientId;
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

            if (selfTrade)
                break;
        }
    }

    result.success = true;
    return result;
}

std::uint64_t ReferenceBook::computeDigest() const {
    std::uint64_t digest = 1469598103934665603ULL;
    auto hash = [&](const auto& levels) {
        for (const auto& [price, queue] : levels)
            for (const auto& o : queue) {
                for (std::uint64_t v :
                     {static_cast<std::uint64_t>(price), o.orderId,
                      static_cast<std::uint64_t>(o.clientId), static_cast<std::uint64_t>(o.side),
                      static_cast<std::uint64_t>(o.quantity),
                      static_cast<std::uint64_t>(o.executedQuantity)})
                    digest = (digest ^ v) * 1099511628211ULL;
            }
    };
    hash(bids_);
    hash(asks_);
    return digest;
}

bool ReferenceBook::canFillFok(const Order& aggressor) const {
    Quantity remaining = aggressor.quantity;
    if (aggressor.side == Side::Buy) {
        for (const auto& [price, queue] : asks_) {
            if (aggressor.price < price)
                return false;
            for (const auto& passive : queue) {
                if (passive.clientId == aggressor.clientId)
                    return false;
                Quantity fillQty = std::min(remaining, passive.remainingQuantity());
                remaining -= fillQty;
                if (remaining == 0)
                    return true;
            }
        }
    } else {
        for (const auto& [price, queue] : bids_) {
            if (aggressor.price > price)
                return false;
            for (const auto& passive : queue) {
                if (passive.clientId == aggressor.clientId)
                    return false;
                Quantity fillQty = std::min(remaining, passive.remainingQuantity());
                remaining -= fillQty;
                if (remaining == 0)
                    return true;
            }
        }
    }
    return false;
}

void ReferenceBook::clear() {
    orders_.clear();
    bids_.clear();
    asks_.clear();
    nextMatchId_ = 1;
}

bool ReferenceBook::installOrder(const Order& order) {
    auto key = std::make_pair(order.clientId, order.orderId);
    if (orders_.count(key) != 0 || order.quantity == 0 || order.remainingQuantity() == 0) {
        return false;
    }
    Order o = order;
    o.status = OrderStatus::Live;
    if (o.side == Side::Buy) {
        bids_[o.price].push_back(o);
    } else {
        asks_[o.price].push_back(o);
    }
    orders_[key] = o;
    return true;
}

}  // namespace lockstep
