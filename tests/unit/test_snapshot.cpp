#include <cassert>
#include <iostream>
#include "lockstep/persistence/snapshot.hpp"

namespace {
void testSnapshotPlaceholder() {
    // Snapshot tests are in test_recovery.cpp
    std::cout << "  [PASS] Snapshot placeholder\n";
}
}  // namespace

int runSnapshotTests() {
    testSnapshotPlaceholder();
    return 0;
}
