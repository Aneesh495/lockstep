#include <cassert>
#include <iostream>
#include "lockstep/network/udp_publisher.hpp"

namespace {
void testUdpPlaceholder() {
    // UDP tests are in test_network.cpp
    std::cout << "  [PASS] UDP placeholder\n";
}
}  // namespace

int runUdpTests() {
    testUdpPlaceholder();
    return 0;
}
