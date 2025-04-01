#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <cassert>
#include <type_traits>
#include <new>

namespace lockstep {

// Bounded cache-line-padded SPSC ring with power-of-two capacity
// Uses acquire/release memory ordering
// Single producer, single consumer only

template <typename T>
class SpscRing {
public:
    explicit SpscRing(std::uint32_t capacity = 1024)
        : capacity_(capacity)
        , mask_(capacity - 1)
        , buffer_(static_cast<T*>(::operator new(sizeof(T) * capacity, std::align_val_t{64})))
        , head_(0)
        , tail_(0)
    {
        // Capacity must be power of 2
        assert((capacity & (capacity - 1)) == 0);
        assert(capacity >= 2);
    }
    
    ~SpscRing() {
        // Destroy remaining elements
        std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        std::uint64_t head = head_.load(std::memory_order_relaxed);
        
        while (tail != head) {
            buffer_[tail & mask_].~T();
            ++tail;
        }
        
        ::operator delete(buffer_, std::align_val_t{64});
    }
    
    // Non-copyable, non-movable
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;
    
    // Producer: try to push an element
    // Returns false if ring is full
    template <typename U>
    bool tryPush(U&& value) {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        
        if (head - tail >= capacity_) {
            return false; // Full
        }
        
        new (&buffer_[head & mask_]) T(std::forward<U>(value));
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    
    // Producer: push an element (blocks/spins until space available)
    template <typename U>
    void push(U&& value) {
        while (!tryPush(std::forward<U>(value))) {
            // Spin or yield
        }
    }
    
    // Producer: push and return whether successful
    // Non-blocking
    template <typename U>
    [[nodiscard]] bool pushIfSpace(U&& value) {
        return tryPush(std::forward<U>(value));
    }
    
    // Consumer: try to pop an element
    // Returns false if ring is empty
    bool tryPop(T& out) {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        
        if (tail == head) {
            return false; // Empty
        }
        
        out = std::move(buffer_[tail & mask_]);
        buffer_[tail & mask_].~T();
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    
    // Consumer: pop an element (blocks/spins until available)
    T pop() {
        T value;
        while (!tryPop(value)) {
            // Spin or yield
        }
        return value;
    }
    
    // Check if empty (approximate, only valid on consumer thread)
    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    // Check if full (approximate, only valid on producer thread)
    bool full() const {
        return head_.load(std::memory_order_acquire) - 
               tail_.load(std::memory_order_acquire) >= capacity_;
    }
    
    // Get number of elements (approximate)
    std::uint64_t size() const {
        std::uint64_t head = head_.load(std::memory_order_acquire);
        std::uint64_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }
    
    // Get capacity
    std::uint32_t capacity() const {
        return capacity_;
    }
    
    // Clear all elements (not thread-safe, only use when paused)
    void clear() {
        std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        std::uint64_t head = head_.load(std::memory_order_relaxed);
        
        while (tail != head) {
            buffer_[tail & mask_].~T();
            ++tail;
        }
        
        tail_.store(0, std::memory_order_relaxed);
        head_.store(0, std::memory_order_relaxed);
    }
    
    // Get approximate available space (producer thread)
    std::uint64_t available() const {
        return capacity_ - size();
    }

private:
    const std::uint32_t capacity_;
    const std::uint32_t mask_;
    T* buffer_;
    
    // Cache line padding to prevent false sharing
    alignas(64) std::atomic<std::uint64_t> head_; // Producer writes
    alignas(64) std::atomic<std::uint64_t> tail_; // Consumer writes
};

// Specialization for POD types (no construction/destruction)
template <typename T>
requires std::is_trivially_copyable_v<T>
class SpscRing<T> {
public:
    explicit SpscRing(std::uint32_t capacity = 1024)
        : capacity_(capacity)
        , mask_(capacity - 1)
        , buffer_(static_cast<T*>(::operator new(sizeof(T) * capacity, std::align_val_t{64})))
        , head_(0)
        , tail_(0)
    {
        assert((capacity & (capacity - 1)) == 0);
        assert(capacity >= 2);
    }
    
    ~SpscRing() {
        ::operator delete(buffer_, std::align_val_t{64});
    }
    
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;
    
    bool tryPush(const T& value) {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        
        if (head - tail >= capacity_) {
            return false;
        }
        
        buffer_[head & mask_] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    
    void push(const T& value) {
        while (!tryPush(value)) {
        }
    }
    
    bool tryPop(T& out) {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        
        if (tail == head) {
            return false;
        }
        
        out = buffer_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    
    T pop() {
        T value;
        while (!tryPop(value)) {
        }
        return value;
    }
    
    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    bool full() const {
        return head_.load(std::memory_order_acquire) - 
               tail_.load(std::memory_order_acquire) >= capacity_;
    }
    
    std::uint64_t size() const {
        return head_.load(std::memory_order_acquire) - 
               tail_.load(std::memory_order_acquire);
    }
    
    std::uint32_t capacity() const { return capacity_; }
    
    void clear() {
        tail_.store(0, std::memory_order_relaxed);
        head_.store(0, std::memory_order_relaxed);
    }

private:
    const std::uint32_t capacity_;
    const std::uint32_t mask_;
    T* buffer_;
    alignas(64) std::atomic<std::uint64_t> head_;
    alignas(64) std::atomic<std::uint64_t> tail_;
};

} // namespace lockstep
