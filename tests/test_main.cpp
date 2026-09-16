#include <iostream>
#include <cstdlib>
#include <csignal>
#include <unistd.h>

// Global flag for timeout
volatile bool g_timeout = false;

void timeout_handler(int sig) {
    g_timeout = true;
    std::cerr << "\n[TIMEOUT] Test took too long\n";
    _exit(1);
}

// Forward declarations of test runners
extern int runEndianTests();
extern int runCrc32cTests();
extern int runCheckedMathTests();
extern int runObjectPoolTests();
extern int runRobinHoodMapTests();
extern int runSpscRingTests();
extern int runOrderBookTests();
extern int runOrderBookDetailTests();
extern int runMatchingEngineTests();
extern int runRiskEngineTests();
extern int runCodecTests();
extern int runWalTests();
extern int runSnapshotTests();
extern int runDifferentialTests();
extern int runNetworkTests();
extern int runRecoveryTests();
extern int runUdpTests();
extern int runGoldenTests();

int main(int argc, char** argv) {
    std::cout << "Lockstep Test Suite\n";
    std::cout << "===================\n\n";
    
    // Set alarm for 300 second timeout (5 minutes)
    std::signal(SIGALRM, timeout_handler);
    alarm(300);
    
    int result = 0;
    
    std::cout << "Running unit tests...\n";
    result |= runEndianTests();
    result |= runCrc32cTests();
    result |= runCheckedMathTests();
    result |= runObjectPoolTests();
    result |= runRobinHoodMapTests();
    result |= runSpscRingTests();
    result |= runOrderBookTests();
    result |= runOrderBookDetailTests();
    result |= runMatchingEngineTests();
    result |= runRiskEngineTests();
    result |= runCodecTests();
    result |= runWalTests();
    result |= runSnapshotTests();
    
    std::cout << "Running property tests...\n";
    result |= runDifferentialTests();
    
    std::cout << "Running integration tests...\n";
    result |= runNetworkTests();
    
    std::cout << "Running recovery tests...\n";
    result |= runRecoveryTests();
    
    std::cout << "Running UDP tests...\n";
    result |= runUdpTests();
    
    std::cout << "Running golden tests...\n";
    result |= runGoldenTests();
    
    // Disable alarm
    alarm(0);
    
    if (result == 0) {
        std::cout << "\nAll tests passed!\n";
    } else {
        std::cout << "\nSome tests failed.\n";
    }
    
    return result;
}
