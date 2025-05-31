#pragma once
#include <limits>
#include <unordered_map>
#include <vector>
#include "lockstep/engine/reference_book.hpp"
namespace lockstep {
struct RefClientExposure {
    ClientId clientId = INVALID_CLIENT_ID;
    Position position = 0;
    Quantity openBuyQuantity = 0, openSellQuantity = 0;
    Notional openBuyNotional = 0, openSellNotional = 0;
    std::uint32_t openOrderCount = 0;
    RiskLimits limits;
};
// Independent map/deque matching and exposure oracle. No production matcher or risk calls.
class IndependentReferenceModel {
   public:
    using Result = ReferenceBook::Result;
    explicit IndependentReferenceModel(bool riskEnabled = false) : riskEnabled_(riskEnabled) {}
    void addInstrument(const InstrumentConfig& cfg) {
        instruments_[cfg.id] = cfg;
        books_[cfg.id] = ReferenceBook();
    }
    void setClientLimits(ClientId id, const RiskLimits& lim) {
        auto& c = clients_[id];
        c.clientId = id;
        c.limits = lim;
        riskEnabled_ = true;
    }
    Result newOrder(const Order& input) {
        auto& client = clients_[input.clientId];
        client.clientId = input.clientId;
        auto it = instruments_.find(input.instrumentId);
        if (it == instruments_.end())
            return reject(RejectionReason::UnknownInstrument);
        const auto& cfg = it->second;
        if (riskEnabled_) {
            auto reason = checkRisk(input, cfg);
            if (reason != RejectionReason::None)
                return reject(reason);
        }
        if (input.price < cfg.minPrice || input.price > cfg.maxPrice ||
            (input.price - cfg.minPrice) % cfg.tickSize)
            return reject(RejectionReason::InvalidPrice);
        if (input.quantity == 0 || input.executedQuantity != 0)
            return reject(RejectionReason::InvalidQuantity);
        if (input.clientId == INVALID_CLIENT_ID || input.orderId == INVALID_ORDER_ID)
            return reject(RejectionReason::InvalidMessage);
        if (input.side != Side::Buy && input.side != Side::Sell)
            return reject(RejectionReason::InvalidSide);
        if (input.tif != TimeInForce::GTC && input.tif != TimeInForce::IOC &&
            input.tif != TimeInForce::FOK)
            return reject(RejectionReason::InvalidTimeInForce);
        auto& book = books_.at(input.instrumentId);
        if (book.findOrder(input.clientId, input.orderId))
            return reject(RejectionReason::DuplicateOrderId);
        if (book.orderCount() >= cfg.maxOrdersPerLevel * cfg.maxPriceLevels)
            return reject(RejectionReason::CapacityExceeded);
        auto q =
            input.side == Side::Buy ? book.bidQuantity(input.price) : book.askQuantity(input.price);
        if (static_cast<std::uint64_t>(q) + input.quantity > UINT32_MAX)
            return reject(RejectionReason::InvalidQuantity);
        Order o = input;
        auto r = book.newOrder(o);
        account(r, input.side);
        return r;
    }
    Result cancelOrder(ClientId client, OrderId id, InstrumentId instrument) {
        auto it = books_.find(instrument);
        if (it == books_.end())
            return reject(RejectionReason::UnknownInstrument);
        auto r = it->second.cancelOrder(client, id);
        recompute();
        return r;
    }
    Result replaceOrder(ClientId client, OrderId oldId, OrderId newId, InstrumentId instrument,
                        Price price, Quantity quantity) {
        auto it = books_.find(instrument);
        if (it == books_.end())
            return reject(RejectionReason::UnknownInstrument);
        auto old = it->second.findOrder(client, oldId);
        if (!old)
            return reject(RejectionReason::OrderNotFound);
        if (riskEnabled_) {
            auto saved = clients_[client];
            auto& c = clients_[client];
            --c.openOrderCount;
            if (old->side == Side::Buy) {
                c.openBuyQuantity -= old->remainingQuantity();
                c.openBuyNotional -= static_cast<Notional>(old->price) * old->remainingQuantity();
            } else {
                c.openSellQuantity -= old->remainingQuantity();
                c.openSellNotional -= static_cast<Notional>(old->price) * old->remainingQuantity();
            }
            Order candidate = *old;
            candidate.price = price;
            candidate.quantity = quantity;
            auto reason = checkRisk(candidate, instruments_.at(instrument));
            clients_[client] = saved;
            if (reason != RejectionReason::None)
                return reject(reason);
        }
        const auto& cfg = instruments_.at(instrument);
        if (price < cfg.minPrice || price > cfg.maxPrice || (price - cfg.minPrice) % cfg.tickSize)
            return reject(RejectionReason::InvalidPrice);
        if (quantity == 0 ||
            static_cast<std::uint64_t>(old->executedQuantity) + quantity > UINT32_MAX)
            return reject(RejectionReason::InvalidQuantity);
        auto r = it->second.replaceOrder(client, oldId, newId, price, quantity);
        account(r, old->side);
        return r;
    }
    void massCancel(ClientId client) {
        for (auto& [id, b] : books_) {
            (void)id;
            b.massCancel(client);
        }
        recompute();
    }
    const ReferenceBook* getBook(InstrumentId id) const {
        auto it = books_.find(id);
        return it == books_.end() ? nullptr : &it->second;
    }
    ReferenceBook* getBook(InstrumentId id) {
        auto it = books_.find(id);
        return it == books_.end() ? nullptr : &it->second;
    }
    const RefClientExposure* getClientExposure(ClientId id) const {
        auto it = clients_.find(id);
        return it == clients_.end() ? nullptr : &it->second;
    }
    const auto& clients() const { return clients_; }

   private:
    static Result reject(RejectionReason reason) {
        Result r;
        r.reason = reason;
        return r;
    }
    RejectionReason checkRisk(const Order& o, const InstrumentConfig& cfg) const {
        const auto& c = clients_.at(o.clientId);
        const auto& l = c.limits;
        if (l.killSwitchActive)
            return RejectionReason::KillSwitchActive;
        if (o.side != Side::Buy && o.side != Side::Sell)
            return RejectionReason::InvalidSide;
        if (!o.quantity)
            return RejectionReason::InvalidQuantity;
        if (o.price < cfg.minPrice || o.price > cfg.maxPrice)
            return RejectionReason::PriceBandViolation;
        if (o.quantity > l.maxOrderQuantity)
            return RejectionReason::MaxOrderQuantityExceeded;
        Notional n = static_cast<Notional>(o.price) * o.quantity;
        if (l.maxOrderNotional > 0 && n > l.maxOrderNotional)
            return RejectionReason::MaxOrderNotionalExceeded;
        if (l.maxOpenOrders > 0 && c.openOrderCount >= l.maxOpenOrders)
            return RejectionReason::MaxOpenOrdersExceeded;
        auto q = static_cast<std::uint64_t>(o.side == Side::Buy ? c.openBuyQuantity
                                                                : c.openSellQuantity) +
                 o.quantity;
        if (q > UINT32_MAX || (l.maxOpenQuantity && q > l.maxOpenQuantity))
            return RejectionReason::MaxOpenQuantityExceeded;
        n += o.side == Side::Buy ? c.openBuyNotional : c.openSellNotional;
        if (l.maxOpenNotional > 0 && n > l.maxOpenNotional)
            return RejectionReason::MaxOpenNotionalExceeded;
        Notional worst = static_cast<Notional>(c.position) +
                         (o.side == Side::Buy ? static_cast<Notional>(o.quantity)
                                              : -static_cast<Notional>(o.quantity));
        if (worst > INT64_MAX || worst < INT64_MIN ||
            (l.maxPosition > 0 && (worst < 0 ? -worst : worst) > l.maxPosition))
            return RejectionReason::MaxPositionExceeded;
        return RejectionReason::None;
    }
    void account(const Result& r, Side side) {
        for (const auto& m : r.matches) {
            clients_[m.aggressiveClientId].position +=
                side == Side::Buy ? m.quantity : -static_cast<Position>(m.quantity);
            clients_[m.passiveClientId].position +=
                side == Side::Buy ? -static_cast<Position>(m.quantity) : m.quantity;
        }
        recompute();
    }
    void recompute() {
        for (auto& [id, c] : clients_) {
            c.clientId = id;
            c.openBuyQuantity = c.openSellQuantity = 0;
            c.openBuyNotional = c.openSellNotional = 0;
            c.openOrderCount = 0;
        }
        for (const auto& [id, b] : books_) {
            (void)id;
            b.forEachOrder([&](const Order& o) {
                auto& c = clients_[o.clientId];
                c.clientId = o.clientId;
                ++c.openOrderCount;
                if (o.side == Side::Buy) {
                    c.openBuyQuantity += o.remainingQuantity();
                    c.openBuyNotional += static_cast<Notional>(o.price) * o.remainingQuantity();
                } else {
                    c.openSellQuantity += o.remainingQuantity();
                    c.openSellNotional += static_cast<Notional>(o.price) * o.remainingQuantity();
                }
            });
        }
    }
    bool riskEnabled_;
    std::unordered_map<InstrumentId, InstrumentConfig> instruments_;
    std::unordered_map<InstrumentId, ReferenceBook> books_;
    std::unordered_map<ClientId, RefClientExposure> clients_;
};
}  // namespace lockstep
