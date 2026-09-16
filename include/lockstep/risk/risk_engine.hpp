#pragma once

#include <cstdint>
#include <unordered_map>
#include <optional>
#include "lockstep/common/types.hpp"
#include "lockstep/common/checked_math.hpp"

namespace lockstep {

// Per-client risk state
struct ClientState {
    ClientId clientId = INVALID_CLIENT_ID;
    
    // Positions (signed)
    std::unordered_map<InstrumentId, Position> positions;
    
    // Open orders
    std::uint32_t openOrderCount = 0;
    Quantity openBuyQuantity = 0;
    Quantity openSellQuantity = 0;
    Notional openBuyNotional = 0;
    Notional openSellNotional = 0;
    
    // Reserved for pending fills
    Notional reservedBuyNotional = 0;
    Notional reservedSellNotional = 0;
    
    // Limits
    RiskLimits limits;
};

// Risk engine with per-client limits
// Runs on engine thread before book mutation
class RiskEngine {
public:
    // Configure limits for a client
    void setClientLimits(ClientId clientId, const RiskLimits& limits);
    
    // Check if a new order passes risk limits
    std::pair<bool, RejectionReason> checkNewOrder(
        ClientId clientId,
        InstrumentId instrumentId,
        Side side,
        Price price,
        Quantity quantity,
        const InstrumentConfig& instrumentConfig);
    
    // Check FOK order preflight
    std::pair<bool, RejectionReason> checkFOKOrder(
        ClientId clientId,
        InstrumentId instrumentId,
        Side side,
        Price price,
        Quantity quantity,
        const InstrumentConfig& instrumentConfig);
    
    // Reserve exposure for a resting order
    void reserveOrder(ClientId clientId, InstrumentId instrumentId, Side side, 
                     Price price, Quantity quantity);
    
    // Release reserved exposure when order is filled/canceled
    void releaseOrder(ClientId clientId, InstrumentId instrumentId, Side side,
                     Price price, Quantity quantity);
    
    // Update position after execution
    void updatePosition(ClientId clientId, InstrumentId instrumentId, Side side,
                       Quantity quantity);
    
    // Mass cancel callback
    void onMassCancel(ClientId clientId);
    
    // Access client state
    ClientState* getClientState(ClientId clientId);
    const ClientState* getClientState(ClientId clientId) const;
    
    // Kill switch
    void setKillSwitch(bool active);
    bool isKillSwitchActive() const { return killSwitchActive_; }
    
    // Check all invariants
    bool checkInvariants(std::string& error) const;
    
    // State digest
    std::uint64_t computeDigest() const;

private:
    std::unordered_map<ClientId, ClientState> clients_;
    bool killSwitchActive_ = false;
    
    Notional computeNotional(Price price, Quantity quantity) const;
    Position getOpenPosition(ClientId clientId, InstrumentId instrumentId) const;
};

} // namespace lockstep
