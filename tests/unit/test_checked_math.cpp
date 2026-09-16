#include "lockstep/common/checked_math.hpp"
#include <iostream>
#include <cassert>
#include <limits>

namespace {

void testAddition() {
    auto r1 = lockstep::checkedAdd<int64_t>(10, 20);
    assert(r1.has_value() && *r1 == 30);
    (void)r1;
    
    auto r2 = lockstep::checkedAdd<uint64_t>(
        std::numeric_limits<uint64_t>::max(), 1);
    assert(!r2.has_value());
    (void)r2;
    
    std::cout << "  [PASS] Checked addition\n";
}

void testSubtraction() {
    auto r1 = lockstep::checkedSub<int64_t>(10, 3);
    assert(r1.has_value() && *r1 == 7);
    (void)r1;
    
    auto r2 = lockstep::checkedSub<uint64_t>(3, 10);
    assert(!r2.has_value()); // unsigned
    (void)r2;
    
    std::cout << "  [PASS] Checked subtraction\n";
}

void testMultiplication() {
    auto r1 = lockstep::checkedMul<int64_t>(100, 200);
    assert(r1.has_value() && *r1 == 20000);
    (void)r1;
    
    auto r2 = lockstep::checkedMul<uint32_t>(
        std::numeric_limits<uint32_t>::max(), 2);
    assert(!r2.has_value());
    (void)r2;
    
    std::cout << "  [PASS] Checked multiplication\n";
}

void testNotional() {
    auto n1 = lockstep::computeNotional(100, 10);
    assert(n1.has_value() && *n1 == 1000);
    (void)n1;
    
    auto n2 = lockstep::computeNotional(-100, 10);
    assert(n2.has_value() && *n2 == -1000);
    (void)n2;
    
    std::cout << "  [PASS] Notional computation\n";
}

}

int runCheckedMathTests() {
    testAddition();
    testSubtraction();
    testMultiplication();
    testNotional();
    return 0;
}
