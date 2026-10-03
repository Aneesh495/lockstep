#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <iostream>

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

#include <cstring>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    int (*runner)();
};

static const TestCase ALL_TESTS[] = {
    {"endian", runEndianTests},
    {"crc32c", runCrc32cTests},
    {"checked_math", runCheckedMathTests},
    {"object_pool", runObjectPoolTests},
    {"robin_hood_map", runRobinHoodMapTests},
    {"spsc", runSpscRingTests},
    {"order_book", runOrderBookTests},
    {"order_book_detail", runOrderBookDetailTests},
    {"matching_engine", runMatchingEngineTests},
    {"risk_engine", runRiskEngineTests},
    {"codec", runCodecTests},
    {"wal", runWalTests},
    {"snapshot", runSnapshotTests},
    {"differential", runDifferentialTests},
    {"network", runNetworkTests},
    {"recovery", runRecoveryTests},
    {"udp", runUdpTests},
    {"golden", runGoldenTests},
};

int main(int argc, char** argv) {
    std::cout << "Lockstep Test Suite\n";
    std::cout << "===================\n\n";

    // Set alarm for 300 second timeout (5 minutes)
    std::signal(SIGALRM, timeout_handler);
    alarm(300);

    std::vector<const TestCase*> testsToRun;

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::string filter = argv[i];
            if (filter == "all" || filter == "LockstepTests") {
                for (const auto& test : ALL_TESTS) {
                    testsToRun.push_back(&test);
                }
                break;
            }
            for (const auto& test : ALL_TESTS) {
                if (std::string(test.name).find(filter) != std::string::npos ||
                    filter.find(test.name) != std::string::npos) {
                    testsToRun.push_back(&test);
                }
            }
        }
        if (testsToRun.empty()) {
            std::cerr << "Error: No tests matched filter\n";
            return 1;
        }
    } else {
        for (const auto& test : ALL_TESTS) {
            testsToRun.push_back(&test);
        }
    }

    int result = 0;
    for (const auto* test : testsToRun) {
        std::cout << "Running " << test->name << " tests...\n";
        result |= test->runner();
    }

    // Disable alarm
    alarm(0);

    if (result == 0) {
        std::cout << "\nAll selected tests passed!\n";
    } else {
        std::cout << "\nSome tests failed.\n";
    }

    return result;
}
