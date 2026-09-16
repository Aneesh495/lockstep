#include "lockstep/engine/matching_engine.hpp"
#include "lockstep/risk/risk_engine.hpp"
#include "lockstep/network/tcp_gateway.hpp"
#include "lockstep/network/udp_publisher.hpp"
#include "lockstep/persistence/wal.hpp"
#include <iostream>
#include <csignal>
#include <atomic>

using namespace lockstep;

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

int main(int argc, char** argv) {
    std::cout << "Lockstep Exchange\n";
    std::cout << "=================\n\n";
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Configuration
    MatchingEngine::Config engineConfig;
    
    InstrumentConfig instr;
    instr.id = 1;
    instr.minPrice = 100;
    instr.maxPrice = 1000;
    engineConfig.instruments.push_back(instr);
    
    MatchingEngine engine(engineConfig);
    RiskEngine risk;
    
    // Network
    TcpGateway gateway(9999, 100);
    UdpPublisher marketData(10001, 10002);
    
    if (!gateway.start()) {
        std::cerr << "Failed to start TCP gateway\n";
        return 1;
    }
    
    if (!marketData.start()) {
        std::cerr << "Failed to start UDP publisher\n";
        return 1;
    }
    
    std::cout << "Exchange running on port 9999\n";
    std::cout << "Market data on ports 10001 (A) and 10002 (B)\n";
    std::cout << "Press Ctrl+C to stop...\n\n";
    
    // Main loop
    while (running) {
        // Process commands from gateway
        CommandMessage cmd;
        while (gateway.commandQueue().tryPop(cmd)) {
            // Process command
            Order order;
            order.clientId = cmd.clientId;
            order.instrumentId = 1;
            
            auto result = engine.newOrder(order);
            
            // Send response
            ResponseMessage resp;
            resp.clientId = cmd.clientId;
            resp.type = MessageType::OrderAccepted;
            resp.engineSeq = result.commandSeq;
            gateway.responseQueue().tryPush(resp);
        }
        
        // Sleep briefly
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    std::cout << "\nShutting down...\n";
    
    gateway.stop();
    marketData.stop();
    
    std::cout << "Exchange stopped.\n";
    
    return 0;
}
