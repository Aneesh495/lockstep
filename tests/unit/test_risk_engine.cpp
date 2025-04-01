#include "lockstep/risk/risk_engine.hpp"
#include <iostream>
#include <cassert>

namespace {

void testQuantityLimit() {
    lockstep::RiskEngine risk;
    
    lockstep::RiskLimits limits;
    limits.clientId = 1;
    limits.maxOrderQuantity = 50;
    risk.setClientLimits(1, limits);
    
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.maxOrdersPerLevel = 100;
    instr.maxPriceLevels = 101;
    
    auto [ok, reason] = risk.checkNewOrder(1, 1, lockstep::Side::Buy, 150, 100, instr);
    assert(!ok);
    (void)ok;
    assert(reason == lockstep::RejectionReason::MaxOrderQuantityExceeded);
    
    std::cout << "  [PASS] Risk engine quantity limit\n";
}

void testPositionLimit() {
    lockstep::RiskEngine risk;
    
    lockstep::RiskLimits limits;
    limits.clientId = 1;
    limits.maxPosition = 100;
    risk.setClientLimits(1, limits);
    
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.maxOrdersPerLevel = 100;
    instr.maxPriceLevels = 101;
    
    auto [ok, reason] = risk.checkNewOrder(1, 1, lockstep::Side::Buy, 150, 150, instr);
    assert(!ok);
    (void)ok;
    assert(reason == lockstep::RejectionReason::MaxPositionExceeded);
    
    std::cout << "  [PASS] Risk engine position limit\n";
}

void testKillSwitch() {
    lockstep::RiskEngine risk;
    
    lockstep::RiskLimits limits;
    limits.clientId = 1;
    risk.setClientLimits(1, limits);
    
    lockstep::InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 200;
    instr.maxOrdersPerLevel = 100;
    instr.maxPriceLevels = 101;
    
    risk.setKillSwitch(true);
    
    auto [ok, reason] = risk.checkNewOrder(1, 1, lockstep::Side::Buy, 150, 10, instr);
    assert(!ok);
    (void)ok;
    assert(reason == lockstep::RejectionReason::KillSwitchActive);
    
    std::cout << "  [PASS] Risk engine kill switch\n";
}

}

int runRiskEngineTests() {
    testQuantityLimit();
    testPositionLimit();
    testKillSwitch();
    return 0;
}
