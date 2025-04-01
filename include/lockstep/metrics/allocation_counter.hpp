#pragma once

#include <cstdint>
#include <atomic>

namespace lockstep {

// Global allocation counter for verifying zero-allocation hot path

class AllocationCounter {
public:
    static std::uint64_t total() { return total_.load(std::memory_order_relaxed); }
    static void increment() { total_.fetch_add(1, std::memory_order_relaxed); }
    static void reset() { total_.store(0, std::memory_order_relaxed); }

private:
    static std::atomic<std::uint64_t> total_;
};

// RAII scope allocation counter
class ScopeAllocationCounter {
public:
    ScopeAllocationCounter() : start_(AllocationCounter::total()) {}
    
    std::uint64_t allocations() const {
        return AllocationCounter::total() - start_;
    }
    
    bool zeroAllocations() const {
        return allocations() == 0;
    }

private:
    std::uint64_t start_;
};

} // namespace lockstep

// Global new/delete overrides for tracking
#ifdef LOCKSTEP_TRACK_ALLOCATIONS
#include <new>

void* operator new(std::size_t size) {
    lockstep::AllocationCounter::increment();
    return std::malloc(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    lockstep::AllocationCounter::increment();
    return std::malloc(size);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}
#endif
