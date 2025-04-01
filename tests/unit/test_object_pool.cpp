#include "lockstep/containers/object_pool.hpp"
#include <iostream>
#include <cassert>

namespace {

struct TestObject {
    int x = 0;
    double y = 0.0;
};

void testAllocateDeallocate() {
    lockstep::ObjectPool<TestObject> pool(10);
    
    auto slot = pool.allocate();
    assert(slot != lockstep::INVALID_SLOT);
    (void)slot;
    
    pool[slot].x = 42;
    pool[slot].y = 3.14;
    
    pool.deallocate(slot);
    assert(pool.size() == 0);
    
    std::cout << "  [PASS] Object pool allocate/deallocate\n";
}

void testExhaustion() {
    lockstep::ObjectPool<TestObject> pool(5);
    
    for (int i = 0; i < 5; ++i) {
        auto slot = pool.allocate();
        assert(slot != lockstep::INVALID_SLOT);
        (void)slot;
    }
    
    auto slot = pool.allocate();
    assert(slot == lockstep::INVALID_SLOT);
    (void)slot;
    assert(pool.full());
    
    std::cout << "  [PASS] Object pool exhaustion\n";
}

void testFreeList() {
    lockstep::ObjectPool<TestObject> pool(10);
    
    auto s1 = pool.allocate();
    auto s2 = pool.allocate();
    auto s3 = pool.allocate();
    (void)s1; (void)s3;
    
    pool.deallocate(s2);
    assert(pool.size() == 2);
    
    auto s4 = pool.allocate();
    (void)s4;
    assert(s4 == s2);
    
    std::cout << "  [PASS] Object pool free list\n";
}

void testVerification() {
    lockstep::ObjectPool<TestObject> pool(10);
    
    for (int i = 0; i < 5; ++i) {
        pool.allocate();
    }
    
    assert(pool.verifyFreeList());
    
    std::cout << "  [PASS] Object pool verification\n";
}

}

int runObjectPoolTests() {
    testAllocateDeallocate();
    testExhaustion();
    testFreeList();
    testVerification();
    return 0;
}
