#include "lockstep/risk/risk_engine.hpp"
#include <map>
#include <string>

namespace lockstep {

void RiskEngine::setClientLimits(ClientId clientId, const RiskLimits& limits) {
    clients_[clientId].clientId = clientId;
    clients_[clientId].limits = limits;
}

std::pair<bool, RejectionReason> RiskEngine::checkNewOrder(
    ClientId clientId, InstrumentId instrumentId, Side side, Price price, Quantity quantity,
    const InstrumentConfig& instrumentConfig) {
    // Check kill switch
    if (killSwitchActive_) {
        return {false, RejectionReason::KillSwitchActive};
    }

    ClientState* state = getClientState(clientId);
    if (!state) {
        return {false, RejectionReason::InvalidMessage};
    }

    if (state->limits.killSwitchActive)
        return {false, RejectionReason::KillSwitchActive};
    if (side != Side::Buy && side != Side::Sell)
        return {false, RejectionReason::InvalidSide};
    if (quantity == 0)
        return {false, RejectionReason::InvalidQuantity};
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
    auto sum =
        checkedAdd(side == Side::Buy ? state->openBuyQuantity : state->openSellQuantity, quantity);
    if (!sum)
        return {false, RejectionReason::MaxOpenQuantityExceeded};
    Quantity newOpenQty = *sum;
    if (state->limits.maxOpenQuantity > 0 && newOpenQty > state->limits.maxOpenQuantity) {
        return {false, RejectionReason::MaxOpenQuantityExceeded};
    }

    // Check max open notional
    Notional newOpenNotional = (side == Side::Buy) ? state->openBuyNotional + notional
                                                   : state->openSellNotional + notional;
    if (state->limits.maxOpenNotional > 0 && newOpenNotional > state->limits.maxOpenNotional) {
        return {false, RejectionReason::MaxOpenNotionalExceeded};
    }

    // Check max position (worst case)
    Position currentPosition = getOpenPosition(clientId, instrumentId);
    Position delta =
        side == Side::Buy ? static_cast<Position>(newOpenQty) : -static_cast<Position>(newOpenQty);
    auto worst = checkedAdd(currentPosition, delta);
    if (!worst)
        return {false, RejectionReason::MaxPositionExceeded};
    Position worstCasePosition = *worst;

    if (state->limits.maxPosition > 0) {
        auto absolute = absPosition(worstCasePosition);
        if (absolute > static_cast<std::uint64_t>(state->limits.maxPosition)) {
            return {false, RejectionReason::MaxPositionExceeded};
        }
    }

    return {true, RejectionReason::None};
}

std::pair<bool, RejectionReason> RiskEngine::checkFOKOrder(
    ClientId clientId, InstrumentId instrumentId, Side side, Price price, Quantity quantity,
    const InstrumentConfig& instrumentConfig) {
    // FOK preflight uses same checks as new order
    return checkNewOrder(clientId, instrumentId, side, price, quantity, instrumentConfig);
}

void RiskEngine::reserveOrder(ClientId clientId, InstrumentId instrumentId, Side side, Price price,
                              Quantity quantity) {
    ClientState* state = getClientState(clientId);
    if (!state)
        return;

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

void RiskEngine::releaseOrder(ClientId clientId, InstrumentId instrumentId, Side side, Price price,
                              Quantity quantity, bool removesOrder) {
    ClientState* state = getClientState(clientId);
    if (!state)
        return;

    Notional notional = computeNotional(price, quantity);

    if (removesOrder && state->openOrderCount > 0) {
        state->openOrderCount--;
    }

    if (side == Side::Buy) {
        state->openBuyQuantity =
            (state->openBuyQuantity > quantity) ? state->openBuyQuantity - quantity : 0;
        state->openBuyNotional =
            (state->openBuyNotional > notional) ? state->openBuyNotional - notional : 0;
    } else {
        state->openSellQuantity =
            (state->openSellQuantity > quantity) ? state->openSellQuantity - quantity : 0;
        state->openSellNotional =
            (state->openSellNotional > notional) ? state->openSellNotional - notional : 0;
    }
}

void RiskEngine::updatePosition(ClientId clientId, InstrumentId instrumentId, Side side,
                                Quantity quantity) {
    ClientState* state = getClientState(clientId);
    if (!state)
        return;

    Position delta =
        (side == Side::Buy) ? static_cast<Position>(quantity) : -static_cast<Position>(quantity);

    state->positions[instrumentId] += delta;
}

void RiskEngine::onMassCancel(ClientId clientId) {
    ClientState* state = getClientState(clientId);
    if (!state)
        return;

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
    std::uint64_t digest = 1469598103934665603ULL;
    auto h = [&](std::uint64_t value) { digest = (digest ^ value) * 1099511628211ULL; };
    auto wide = [&](Notional value) {
        h(static_cast<std::uint64_t>(value));
        h(static_cast<std::uint64_t>(value >> 64));
    };
    h(killSwitchActive_);
    std::map<ClientId, const ClientState*> sorted;
    for (const auto& [id, c] : clients_)
        sorted[id] = &c;
    for (const auto& [id, ptr] : sorted) {
        const auto& c = *ptr;
        h(id);
        h(c.openOrderCount);
        h(c.openBuyQuantity);
        h(c.openSellQuantity);
        wide(c.openBuyNotional);
        wide(c.openSellNotional);
        wide(c.reservedBuyNotional);
        wide(c.reservedSellNotional);
        h(c.limits.maxOrderQuantity);
        h(c.limits.maxOpenOrders);
        h(c.limits.maxOpenQuantity);
        wide(c.limits.maxOrderNotional);
        wide(c.limits.maxOpenNotional);
        h(static_cast<std::uint64_t>(c.limits.maxPosition));
        h(c.limits.killSwitchActive);
        std::map<InstrumentId, Position> positions(c.positions.begin(), c.positions.end());
        for (const auto& [inst, pos] : positions) {
            h(inst);
            h(static_cast<std::uint64_t>(pos));
        }
    }
    return digest;
}

Notional RiskEngine::computeNotional(Price price, Quantity quantity) const {
    return static_cast<Notional>(price) * static_cast<Notional>(quantity);
}

Position RiskEngine::getOpenPosition(ClientId clientId, InstrumentId instrumentId) const {
    const ClientState* state = getClientState(clientId);
    if (!state)
        return 0;

    auto it = state->positions.find(instrumentId);
    return (it != state->positions.end()) ? it->second : 0;
}

void RiskEngine::clear() {
    clients_.clear();
    killSwitchActive_ = false;
}

void RiskEngine::installClientState(const ClientState& state) {
    clients_[state.clientId] = state;
}

}  // namespace lockstep
