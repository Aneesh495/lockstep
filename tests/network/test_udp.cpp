#include "lockstep/network/udp_publisher.hpp"
#include <iostream>
#include <cassert>

namespace {
void testUdpPlaceholder() {
    // UDP tests are in test_network.cpp
    std::cout << "  [PASS] UDP placeholder\n";
}
}

int runUdpTests() {
    testUdpPlaceholder();
    return 0;
}
