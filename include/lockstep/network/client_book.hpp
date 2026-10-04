#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include "lockstep/common/types.hpp"
#include "lockstep/protocol/codec.hpp"

namespace lockstep {

// Client-side reconstructed order book driven by market data events
class ClientBook {
   public:
    explicit ClientBook(InstrumentId instrumentId = 1) : instrumentId_(instrumentId) {}

    InstrumentId instrumentId() const { return instrumentId_; }

    void applyEvent(MessageType type, const std::uint8_t* data, std::size_t size) {
        if (type == MessageType::BookAdd) {
            auto dec = Codec::decodeBookAdd(data, size);
            if (dec && dec->instrumentId == instrumentId_) {
                if (dec->side == Side::Buy) {
                    bids_[dec->price] += dec->quantity;
                } else {
                    asks_[dec->price] += dec->quantity;
                }
                lastEngineSeq_ = dec->engineSeq;
                appliedEvents_++;
            }
        } else if (type == MessageType::BookChange) {
            auto dec = Codec::decodeBookChange(data, size);
            if (dec && dec->instrumentId == instrumentId_) {
                if (dec->side == Side::Buy) {
                    if (dec->newQuantity == 0) {
                        bids_.erase(dec->price);
                    } else {
                        bids_[dec->price] = dec->newQuantity;
                    }
                } else {
                    if (dec->newQuantity == 0) {
                        asks_.erase(dec->price);
                    } else {
                        asks_[dec->price] = dec->newQuantity;
                    }
                }
                lastEngineSeq_ = dec->engineSeq;
                appliedEvents_++;
            }
        } else if (type == MessageType::BookDelete) {
            auto dec = Codec::decodeBookDelete(data, size);
            if (dec && dec->instrumentId == instrumentId_) {
                if (dec->side == Side::Buy) {
                    bids_.erase(dec->price);
                } else {
                    asks_.erase(dec->price);
                }
                lastEngineSeq_ = dec->engineSeq;
                appliedEvents_++;
            }
        } else if (type == MessageType::Trade) {
            auto dec = Codec::decodeTrade(data, size);
            if (dec && dec->instrumentId == instrumentId_) {
                lastEngineSeq_ = dec->engineSeq;
                totalTrades_++;
                appliedEvents_++;
            }
        }
    }

    void clear() {
        bids_.clear();
        asks_.clear();
        lastEngineSeq_ = 0;
        appliedEvents_ = 0;
        totalTrades_ = 0;
    }

    Price bestBid() const { return bids_.empty() ? 0 : bids_.begin()->first; }

    Price bestAsk() const { return asks_.empty() ? 0 : asks_.begin()->first; }

    Quantity bidQuantity(Price p) const {
        auto it = bids_.find(p);
        return (it != bids_.end()) ? it->second : 0;
    }

    Quantity askQuantity(Price p) const {
        auto it = asks_.find(p);
        return (it != asks_.end()) ? it->second : 0;
    }

    Quantity totalBidQuantity() const {
        Quantity total = 0;
        for (const auto& [p, q] : bids_) {
            total += q;
        }
        return total;
    }

    Quantity totalAskQuantity() const {
        Quantity total = 0;
        for (const auto& [p, q] : asks_) {
            total += q;
        }
        return total;
    }

    std::uint64_t lastEngineSeq() const { return lastEngineSeq_; }
    std::uint64_t appliedEvents() const { return appliedEvents_; }
    std::uint64_t totalTrades() const { return totalTrades_; }

    std::uint64_t computeDigest() const {
        std::uint64_t digest = 0;
        for (const auto& [p, q] : bids_) {
            digest ^= static_cast<std::uint64_t>(p) * 0x9e3779b97f4a7c15ULL;
            digest ^= static_cast<std::uint64_t>(q) * 0xbf58476d1ce4e5b9ULL;
        }
        for (const auto& [p, q] : asks_) {
            digest ^= static_cast<std::uint64_t>(p) * 0x94d049bb133111ebULL;
            digest ^= static_cast<std::uint64_t>(q) * 0x9ddfea08eb382d69ULL;
        }
        return digest;
    }

    const std::map<Price, Quantity, std::greater<Price>>& bids() const { return bids_; }
    const std::map<Price, Quantity, std::less<Price>>& asks() const { return asks_; }

   private:
    InstrumentId instrumentId_ = 1;
    std::map<Price, Quantity, std::greater<Price>> bids_;
    std::map<Price, Quantity, std::less<Price>> asks_;
    std::uint64_t lastEngineSeq_ = 0;
    std::uint64_t appliedEvents_ = 0;
    std::uint64_t totalTrades_ = 0;
};

}  // namespace lockstep
