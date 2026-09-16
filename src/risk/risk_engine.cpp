#include "lockstep/risk/risk_engine.hpp"
#include <string>

namespace lockstep {

void RiskEngine::setClientLimits(ClientId clientId, const RiskLimits& limits) {
    clients_[clientId].clientId = clientId;
    clients_[clientId].limits = limits;
}

std::pair<bool, RejectionReason> RiskEngine::checkNewOrder(
    ClientId clientId,
    InstrumentId instrumentId,
    Side side,
    Price price,
    Quantity quantity,
    const InstrumentConfig& instrumentConfig) {
    
    // Check kill switch
    if (killSwitchActive_) {
        return {false, RejectionReason::KillSwitchActive};
    }
    
    ClientState* state = getClientState(clientId);
    if (!state) {
        return {false, RejectionReason::InvalidMessage};
    }
    
    // Check instrument is legal
    if (instrumentConfig.id != instrumentId) {
        return {false, RejectionReason::UnknownInstrument};
    }
    
    // Check price band
    if (price < instrumentConfig.minPrice || price > instrumentConfig.maxPrice) {
        return {false, RejectionReason::PriceBandViolation};
    }
    
    // Check max order quantity
    if (quantity > state->limits.maxOrderQuantity) {
        return {false, RejectionReason::MaxOrderQuantityExceeded};
    }
    
    // Check max order notional
    Notional notional = computeNotional(price, quantity);
    if (state->limits.maxOrderNotional > 0 && notional > state->limits.maxOrderNotional) {
        return {false, RejectionReason::MaxOrderNotionalExceeded};
    }
    
    // Check max open orders
    if (state->limits.maxOpenOrders > 0 && state->openOrderCount >= state->limits.maxOpenOrders) {
        return {false, RejectionReason::MaxOpenOrdersExceeded};
    }
    
    // Check max open quantity
    Quantity newOpenQty = (side == Side::Buy) ? 
        state->openBuyQuantity + quantity : state->openSellQuantity + quantity;
    if (state->limits.maxOpenQuantity > 0 && newOpenQty > state->limits.maxOpenQuantity) {
        return {false, RejectionReason::MaxOpenQuantityExceeded};
    }
    
    // Check max open notional
    Notional newOpenNotional = (side == Side::Buy) ?
        state->openBuyNotional + notional : state->openSellNotional + notional;
    if (state->limits.maxOpenNotional > 0 && newOpenNotional > state->limits.maxOpenNotional) {
        return {false, RejectionReason::MaxOpenNotionalExceeded};
    }
    
    // Check max position (worst case)
    Position currentPosition = getOpenPosition(clientId, instrumentId);
    Position worstCasePosition = (side == Side::Buy) ?
        currentPosition + static_cast<Position>(quantity) :
        currentPosition - static_cast<Position>(quantity);
    
    if (state->limits.maxPosition > 0) {
        Position absWorstCase = (worstCasePosition >= 0) ? worstCasePosition : -worstCasePosition;
        if (absWorstCase > static_cast<Position>(state->limits.maxPosition)) {
            return {false, RejectionReason::MaxPositionExceeded};
        }
    }
    
    return {true, RejectionReason::None};
}

std::pair<bool, RejectionReason> RiskEngine::checkFOKOrder(
    ClientId clientId,
    InstrumentId instrumentId,
    Side side,
    Price price,
    Quantity quantity,
    const InstrumentConfig& instrumentConfig) {
    // FOK preflight uses same checks as new order
    return checkNewOrder(clientId, instrumentId, side, price, quantity, instrumentConfig);
}

void RiskEngine::reserveOrder(ClientId clientId, InstrumentId instrumentId, Side side,
                               Price price, Quantity quantity) {
    ClientState* state = getClientState(clientId);
    if (!state) return;
    
    Notional notional = computeNotional(price, quantity);
    
    state->openOrderCount++;
    
    if (side == Side::Buy) {
        state->openBuyQuantity += quantity;
        state->openBuyNotional += notional;
    } else {
        state->openSellQuantity += quantity;
        state->openSellNotional += notional;
    }
}

void RiskEngine::releaseOrder(ClientId clientId, InstrumentId instrumentId, Side side,
                               Price price, Quantity quantity) {
    ClientState* state = getClientState(clientId);
    if (!state) return;
    
    Notional notional = computeNotional(price, quantity);
    
    if (state->openOrderCount > 0) {
        state->openOrderCount--;
    }
    
    if (side == Side::Buy) {
        state->openBuyQuantity = (state->openBuyQuantity > quantity) ? 
            state->openBuyQuantity - quantity : 0;
        state->openBuyNotional = (state->openBuyNotional > notional) ?
            state->openBuyNotional - notional : 0;
    } else {
        state->openSellQuantity = (state->openSellQuantity > quantity) ?
            state->openSellQuantity - quantity : 0;
        state->openSellNotional = (state->openSellNotional > notional) ?
            state->openSellNotional - notional : 0;
    }
}

void RiskEngine::updatePosition(ClientId clientId, InstrumentId instrumentId, Side side,
                                 Quantity quantity) {
    ClientState* state = getClientState(clientId);
    if (!state) return;
    
    Position delta = (side == Side::Buy) ? 
        static_cast<Position>(quantity) : -static_cast<Position>(quantity);
    
    state->positions[instrumentId] += delta;
    
    // Update open quantity (filled portion no longer open)
    if (side == Side::Buy) {
        state->openBuyQuantity = (state->openBuyQuantity > quantity) ?
            state->openBuyQuantity - quantity : 0;
    } else {
        state->openSellQuantity = (state->openSellQuantity > quantity) ?
            state->openSellQuantity - quantity : 0;
    }
}

void RiskEngine::onMassCancel(ClientId clientId) {
    ClientState* state = getClientState(clientId);
    if (!state) return;
    
    state->openOrderCount = 0;
    state->openBuyQuantity = 0;
    state->openSellQuantity = 0;
    state->openBuyNotional = 0;
    state->openSellNotional = 0;
}

ClientState* RiskEngine::getClientState(ClientId clientId) {
    auto it = clients_.find(clientId);
    return (it != clients_.end()) ? &it->second : nullptr;
}

const ClientState* RiskEngine::getClientState(ClientId clientId) const {
    auto it = clients_.find(clientId);
    return (it != clients_.end()) ? &it->second : nullptr;
}

void RiskEngine::setKillSwitch(bool active) {
    killSwitchActive_ = active;
}

bool RiskEngine::checkInvariants(std::string& error) const {
    for (const auto& [clientId, state] : clients_) {
        // Check open quantity matches notional
        if (state.openBuyQuantity > 0 && state.openBuyNotional <= 0) {
            error = "Open buy quantity but no notional for client " + std::to_string(clientId);
            return false;
        }
        
        if (state.openSellQuantity > 0 && state.openSellNotional <= 0) {
            error = "Open sell quantity but no notional for client " + std::to_string(clientId);
            return false;
        }
    }
    
    return true;
}

std::uint64_t RiskEngine::computeDigest() const {
    std::uint64_t digest = 0;
    
    for (const auto& [clientId, state] : clients_) {
        digest ^= clientId * 0x9e3779b97f4a7c15ULL;
        digest ^= static_cast<std::uint64_t>(state.openOrderCount) * 0xbf58476d1ce4e5b9ULL;
        
        for (const auto& [instId, pos] : state.positions) {
            digest ^= instId * 0x94d049bb133111ebULL;
            digest ^= static_cast<std::uint64_t>(pos) * 0x9ddfea08eb382d69ULL;
        }
    }
    
    return digest;
}

Notional RiskEngine::computeNotional(Price price, Quantity quantity) const {
    return static_cast<Notional>(price) * static_cast<Notional>(quantity);
}

Position RiskEngine::getOpenPosition(ClientId clientId, InstrumentId instrumentId) const {
    const ClientState* state = getClientState(clientId);
    if (!state) return 0;
    
    auto it = state->positions.find(instrumentId);
    return (it != state->positions.end()) ? it->second : 0;
}

} // namespace lockstep
