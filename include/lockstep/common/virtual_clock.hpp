#pragma once

#include "lockstep/common/types.hpp"
#include <cstdint>
#include <atomic>

namespace lockstep {

// Deterministic virtual clock for engine logic
// Uses monotonic integer nanoseconds
// Never reads wall-clock time inside deterministic engine logic

class VirtualClock {
public:
    explicit VirtualClock(Timestamp initial = 0) 
        : time_(initial) 
    {}
    
    // Get current virtual time
    Timestamp now() const {
        return time_.load(std::memory_order_relaxed);
    }
    
    // Advance time by a delta (nanoseconds)
    void advance(std::uint64_t delta) {
        time_.fetch_add(delta, std::memory_order_relaxed);
    }
    
    // Set time to specific value
    void set(Timestamp t) {
        time_.store(t, std::memory_order_relaxed);
    }
    
    // Reset to zero
    void reset() {
        time_.store(0, std::memory_order_relaxed);
    }
    
    // Create a timestamp for an event
    Timestamp timestamp() {
        return time_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Batch advance for testing
    void advanceBatch(std::uint64_t count, std::uint64_t deltaPer) {
        time_.fetch_add(count * deltaPer, std::memory_order_relaxed);
    }

private:
    std::atomic<Timestamp> time_;
};

// Non-atomic version for single-threaded deterministic replay
class DeterministicClock {
public:
    explicit DeterministicClock(Timestamp initial = 0) 
        : time_(initial) 
    {}
    
    Timestamp now() const { return time_; }
    void advance(std::uint64_t delta) { time_ += delta; }
    void set(Timestamp t) { time_ = t; }
    void reset() { time_ = 0; }
    Timestamp timestamp() { return time_++; }

private:
    Timestamp time_;
};

} // namespace lockstep
