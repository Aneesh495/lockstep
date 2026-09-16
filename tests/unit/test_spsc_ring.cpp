#include "lockstep/concurrency/spsc_ring.hpp"
#include <iostream>
#include <cassert>
#include <thread>

namespace {

void testBasicTransfer() {
    lockstep::SpscRing<int> ring(4);
    
    assert(ring.empty());
    assert(!ring.full());
    
    assert(ring.tryPush(42));
    
    assert(!ring.empty());
    
    int value = 0;
    assert(ring.tryPop(value));
    assert(value == 42);
    (void)value;
    
    assert(ring.empty());
    
    std::cout << "  [PASS] SPSC basic transfer\n";
}

void testFullEmpty() {
    lockstep::SpscRing<int> ring(4);
    
    assert(ring.tryPush(1));
    assert(ring.tryPush(2));
    assert(ring.tryPush(3));
    assert(ring.tryPush(4));
    
    assert(ring.full());
    assert(!ring.tryPush(5));
    
    int value = 0;
    assert(ring.tryPop(value));
    (void)value;
    assert(ring.tryPush(5));
    
    std::cout << "  [PASS] SPSC full/empty\n";
}

void testWraparound() {
    lockstep::SpscRing<int> ring(4);
    
    for (int i = 0; i < 100; ++i) {
        assert(ring.tryPush(i));
        int value = 0;
        assert(ring.tryPop(value));
        assert(value == i);
        (void)value;
    }
    
    std::cout << "  [PASS] SPSC wraparound\n";
}

}

int runSpscRingTests() {
    testBasicTransfer();
    testFullEmpty();
    testWraparound();
    return 0;
}
