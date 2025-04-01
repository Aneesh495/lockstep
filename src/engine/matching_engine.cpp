#include "lockstep/engine/matching_engine.hpp"
#include <algorithm>

namespace lockstep {

MatchingEngine::MatchingEngine(const Config& config)
    : config_(config)
{
    for (const auto& instr : config.instruments) {
        instrumentConfigs_[instr.id] = instr;
        
        OrderBook::Config bookConfig;
        bookConfig.instrumentId = instr.id;
        bookConfig.minPrice = instr.minPrice;
        bookConfig.maxPrice = instr.maxPrice;
        bookConfig.tickSize = instr.tickSize;
        bookConfig.maxOrders = instr.maxOrdersPerLevel * instr.maxPriceLevels;
        
        books_[instr.id] = std::make_unique<OrderBook>(bookConfig);
        
        if (config.useReferenceBook) {
            refBooks_[instr.id] = std::make_unique<ReferenceBook>();
        }
    }
}

MatchingEngine::Result MatchingEngine::newOrder(const Order& order) {
    Result result;
    
    // Check kill switch
    if (killSwitchActive_) {
        result.reason = RejectionReason::KillSwitchActive;
        return result;
    }
    
    // Find instrument
    auto it = books_.find(order.instrumentId);
    if (it == books_.end()) {
        result.reason = RejectionReason::UnknownInstrument;
        return result;
    }
    
    OrderBook& book = *it->second;
    
    // Assign sequences
    result.commandSeq = nextCommandSeq();
    
    // Execute on optimized book
    Order mutableOrder = order;
    mutableOrder.engineSeq = result.commandSeq;
    mutableOrder.createTime = clock_.now();
    
    auto bookResult = book.newOrder(mutableOrder);
    
    if (!bookResult.success) {
        result.reason = bookResult.reason;
        return result;
    }
    
    result.success = true;
    result.matches = std::move(bookResult.matches);
    
    // Update statistics
    totalMatches_ += result.matches.size();
    
    // Update event sequence
    for (auto& match : result.matches) {
        match.timestamp = clock_.now();
        result.eventSeq = nextEventSeq();
    }
    
    // Also execute on reference book for differential testing
    if (config_.useReferenceBook) {
        auto refIt = refBooks_.find(order.instrumentId);
        if (refIt != refBooks_.end()) {
            Order refOrder = order;
            refIt->second->newOrder(refOrder);
        }
    }
    
    return result;
}

MatchingEngine::Result MatchingEngine::cancelOrder(ClientId clientId, OrderId orderId, InstrumentId instrumentId) {
    Result result;
    result.commandSeq = nextCommandSeq();
    
    if (instrumentId != INVALID_INSTRUMENT_ID) {
        auto it = books_.find(instrumentId);
        if (it == books_.end()) {
            result.reason = RejectionReason::UnknownInstrument;
            return result;
        }
        
        auto bookResult = it->second->cancelOrder(clientId, orderId);
        result.success = bookResult.success;
        result.reason = bookResult.reason;
        
        if (config_.useReferenceBook) {
            auto refIt = refBooks_.find(instrumentId);
            if (refIt != refBooks_.end()) {
                refIt->second->cancelOrder(clientId, orderId);
            }
        }
    } else {
        // Search all instruments
        for (auto& [id, book] : books_) {
            auto bookResult = book->cancelOrder(clientId, orderId);
            if (bookResult.success) {
                result.success = true;
                
                if (config_.useReferenceBook) {
                    auto refIt = refBooks_.find(id);
                    if (refIt != refBooks_.end()) {
                        refIt->second->cancelOrder(clientId, orderId);
                    }
                }
                break;
            }
        }
        
        if (!result.success) {
            result.reason = RejectionReason::OrderNotFound;
        }
    }
    
    return result;
}

MatchingEngine::Result MatchingEngine::replaceOrder(ClientId clientId, OrderId oldOrderId, OrderId newOrderId,
                                                     InstrumentId instrumentId, Price newPrice, Quantity newQuantity) {
    Result result;
    result.commandSeq = nextCommandSeq();
    
    auto it = books_.find(instrumentId);
    if (it == books_.end()) {
        result.reason = RejectionReason::UnknownInstrument;
        return result;
    }
    
    auto bookResult = it->second->replaceOrder(clientId, oldOrderId, newOrderId, newPrice, newQuantity);
    result.success = bookResult.success;
    result.reason = bookResult.reason;
    
    if (config_.useReferenceBook) {
        auto refIt = refBooks_.find(instrumentId);
        if (refIt != refBooks_.end()) {
            refIt->second->replaceOrder(clientId, oldOrderId, newOrderId, newPrice, newQuantity);
        }
    }
    
    return result;
}

std::uint32_t MatchingEngine::massCancel(ClientId clientId) {
    std::uint32_t total = 0;
    
    for (auto& [id, book] : books_) {
        total += book->massCancel(clientId);
        
        if (config_.useReferenceBook) {
            auto refIt = refBooks_.find(id);
            if (refIt != refBooks_.end()) {
                refIt->second->massCancel(clientId);
            }
        }
    }
    
    return total;
}

void MatchingEngine::setKillSwitch(bool active) {
    killSwitchActive_ = active;
}

OrderBook* MatchingEngine::getBook(InstrumentId instrumentId) {
    auto it = books_.find(instrumentId);
    return (it != books_.end()) ? it->second.get() : nullptr;
}

const OrderBook* MatchingEngine::getBook(InstrumentId instrumentId) const {
    auto it = books_.find(instrumentId);
    return (it != books_.end()) ? it->second.get() : nullptr;
}

std::uint32_t MatchingEngine::totalOrderCount() const {
    std::uint32_t total = 0;
    for (const auto& [id, book] : books_) {
        total += book->orderCount();
    }
    return total;
}

std::uint64_t MatchingEngine::computeStateDigest() const {
    std::uint64_t digest = 0;
    
    // Hash instrument configs in order
    for (const auto& instr : config_.instruments) {
        digest ^= static_cast<std::uint64_t>(instr.id) * 0x9e3779b97f4a7c15ULL;
        digest ^= static_cast<std::uint64_t>(instr.tickSize) * 0xbf58476d1ce4e5b9ULL;
    }
    
    // Hash each book
    for (const auto& [id, book] : books_) {
        digest ^= book->computeDigest();
    }
    
    // Hash sequences
    digest ^= commandSeq_ * 0x94d049bb133111ebULL;
    digest ^= eventSeq_ * 0x9ddfea08eb382d69ULL;
    
    return digest;
}

bool MatchingEngine::verifyAgainstReference(std::string& error) const {
    if (!config_.useReferenceBook) {
        error = "Reference book disabled";
        return false;
    }
    
    for (const auto& [id, book] : books_) {
        auto refIt = refBooks_.find(id);
        if (refIt == refBooks_.end()) continue;
        
        if (book->orderCount() != refIt->second->orderCount()) {
            error = "Order count mismatch for instrument " + std::to_string(id);
            return false;
        }
        
        if (book->computeDigest() != refIt->second->computeDigest()) {
            error = "State digest mismatch for instrument " + std::to_string(id);
            return false;
        }
    }
    
    return true;
}

bool MatchingEngine::checkInvariants(std::string& error) const {
    for (const auto& [id, book] : books_) {
        if (!book->checkInvariants(error)) {
            error = "Instrument " + std::to_string(id) + ": " + error;
            return false;
        }
    }
    
    return true;
}

} // namespace lockstep
