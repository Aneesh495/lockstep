#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>
#include "lockstep/common/types.hpp"

namespace lockstep {

// Fixed-capacity index-based object pool with free list
// Zero heap allocations after initialization
// All indices are stable - use INVALID_SLOT as sentinel

template <typename T>
class ObjectPool {
public:
    // Node storing object and free-list linkage
    struct Node {
        T object;
        SlotIndex nextFree;
        SlotIndex prevFree;
        bool allocated;
        
        Node() : nextFree(INVALID_SLOT), prevFree(INVALID_SLOT), allocated(false) {}
    };
    
    explicit ObjectPool(std::uint32_t capacity = 1024)
        : capacity_(capacity)
        , size_(0)
        , freeHead_(INVALID_SLOT)
        , nodes_(capacity)
    {
        // Initialize free list (all slots initially free)
        for (std::uint32_t i = 0; i < capacity; ++i) {
            nodes_[i].nextFree = (i + 1 < capacity) ? i + 1 : INVALID_SLOT;
            nodes_[i].prevFree = (i > 0) ? i - 1 : INVALID_SLOT;
            nodes_[i].allocated = false;
        }
        freeHead_ = (capacity > 0) ? 0 : INVALID_SLOT;
    }
    
    // Allocate a slot and return its index
    // Returns INVALID_SLOT if pool is full
    SlotIndex allocate() {
        if (freeHead_ == INVALID_SLOT) {
            return INVALID_SLOT;
        }
        
        SlotIndex slot = freeHead_;
        Node& node = nodes_[slot];
        
        // Update free list head
        freeHead_ = node.nextFree;
        
        // Update next node's prev pointer
        if (freeHead_ != INVALID_SLOT) {
            nodes_[freeHead_].prevFree = INVALID_SLOT;
        }
        
        // Mark as allocated
        node.allocated = true;
        node.nextFree = INVALID_SLOT;
        node.prevFree = INVALID_SLOT;
        
        ++size_;
        return slot;
    }
    
    // Deallocate a slot
    void deallocate(SlotIndex slot) {
        assert(slot < capacity_);
        assert(nodes_[slot].allocated);
        
        Node& node = nodes_[slot];
        node.allocated = false;
        
        // Add to head of free list
        node.nextFree = freeHead_;
        node.prevFree = INVALID_SLOT;
        
        if (freeHead_ != INVALID_SLOT) {
            nodes_[freeHead_].prevFree = slot;
        }
        
        freeHead_ = slot;
        --size_;
    }
    
    // Access object by slot
    T& operator[](SlotIndex slot) {
        assert(slot < capacity_);
        assert(nodes_[slot].allocated);
        return nodes_[slot].object;
    }
    
    const T& operator[](SlotIndex slot) const {
        assert(slot < capacity_);
        assert(nodes_[slot].allocated);
        return nodes_[slot].object;
    }
    
    // Check if slot is allocated
    bool isAllocated(SlotIndex slot) const {
        if (slot >= capacity_) return false;
        return nodes_[slot].allocated;
    }
    
    // Get raw node for intrusive data structures
    Node& node(SlotIndex slot) {
        assert(slot < capacity_);
        return nodes_[slot];
    }
    
    const Node& node(SlotIndex slot) const {
        assert(slot < capacity_);
        return nodes_[slot];
    }
    
    // Capacity and size
    std::uint32_t capacity() const { return capacity_; }
    std::uint32_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == capacity_; }
    
    // Iterate over allocated slots
    template <typename Func>
    void forEach(Func&& func) {
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            if (nodes_[i].allocated) {
                func(i, nodes_[i].object);
            }
        }
    }
    
    template <typename Func>
    void forEach(Func&& func) const {
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            if (nodes_[i].allocated) {
                func(i, nodes_[i].object);
            }
        }
    }
    
    // Clear all slots
    void clear() {
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            nodes_[i].allocated = false;
            nodes_[i].nextFree = (i + 1 < capacity_) ? i + 1 : INVALID_SLOT;
            nodes_[i].prevFree = (i > 0) ? i - 1 : INVALID_SLOT;
        }
        freeHead_ = (capacity_ > 0) ? 0 : INVALID_SLOT;
        size_ = 0;
    }
    
    // Verify integrity of free list (for testing)
    bool verifyFreeList() const {
        std::uint32_t freeCount = 0;
        SlotIndex current = freeHead_;
        SlotIndex prev = INVALID_SLOT;
        
        while (current != INVALID_SLOT) {
            if (current >= capacity_) return false;
            if (nodes_[current].allocated) return false;
            if (nodes_[current].prevFree != prev) return false;
            
            prev = current;
            current = nodes_[current].nextFree;
            ++freeCount;
        }
        
        return (freeCount + size_) == capacity_;
    }

private:
    std::uint32_t capacity_;
    std::uint32_t size_;
    SlotIndex freeHead_;
    std::vector<Node> nodes_;
};

} // namespace lockstep
