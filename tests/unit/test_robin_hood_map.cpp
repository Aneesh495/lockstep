#include "lockstep/containers/fixed_robin_hood_map.hpp"
#include <iostream>
#include <cassert>

namespace {

void testInsertFind() {
    lockstep::FixedRobinHoodMap<lockstep::OrderKey, lockstep::SlotIndex> map(16);
    
    lockstep::OrderKey key{1, 100};
    assert(map.insert(key, 42));
    
    auto value = map.find(key);
    assert(value.has_value());
    assert(*value == 42);
    (void)value;
    
    std::cout << "  [PASS] Robin Hood map insert/find\n";
}

void testErase() {
    lockstep::FixedRobinHoodMap<lockstep::OrderKey, lockstep::SlotIndex> map(16);
    
    lockstep::OrderKey key1{1, 100};
    lockstep::OrderKey key2{1, 101};
    
    map.insert(key1, 5);
    map.insert(key2, 10);
    
    assert(map.erase(key1));
    assert(!map.find(key1).has_value());
    assert(map.find(key2).has_value());
    
    std::cout << "  [PASS] Robin Hood map erase\n";
}

void testCollision() {
    lockstep::FixedRobinHoodMap<lockstep::OrderKey, lockstep::SlotIndex> map(16);
    
    for (uint32_t i = 0; i < 10; ++i) {
        lockstep::OrderKey key{1, i};
        assert(map.insert(key, i));
        (void)key;
    }
    
    for (uint32_t i = 0; i < 10; ++i) {
        lockstep::OrderKey key{1, i};
        auto value = map.find(key);
        assert(value.has_value() && *value == static_cast<SlotIndex>(i));
        (void)value;
        (void)key;
    }
    
    std::cout << "  [PASS] Robin Hood map collision\n";
}

void testCapacity() {
    lockstep::FixedRobinHoodMap<lockstep::OrderKey, lockstep::SlotIndex> map(16);
    
    for (uint32_t i = 0; i < 16; ++i) {
        lockstep::OrderKey key{1, i};
        assert(map.insert(key, i));
        (void)key;
    }
    
    {
        lockstep::OrderKey key{1, 100};
        assert(!map.insert(key, 100));
        (void)key;
    }
    
    std::cout << "  [PASS] Robin Hood map capacity\n";
}

void testIntegrity() {
    lockstep::FixedRobinHoodMap<lockstep::OrderKey, lockstep::SlotIndex> map(64);
    
    for (uint32_t i = 0; i < 50; ++i) {
        lockstep::OrderKey key{1, i};
        map.insert(key, i);
        (void)key;
    }
    
    assert(map.verifyIntegrity());
    
    for (uint32_t i = 0; i < 25; ++i) {
        lockstep::OrderKey key{1, i * 2};
        map.erase(key);
        (void)key;
    }
    
    assert(map.verifyIntegrity());
    
    std::cout << "  [PASS] Robin Hood map integrity\n";
}

}

int runRobinHoodMapTests() {
    testInsertFind();
    testErase();
    testCollision();
    testCapacity();
    testIntegrity();
    return 0;
}
