#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <optional>
#include "lockstep/common/types.hpp"

namespace lockstep {

// Fixed-capacity Robin Hood hash table
// Uses backward-shift deletion for good cache performance
// Zero heap allocations after initialization
// Key type: pair<ClientId, OrderId> for order lookup

struct OrderKey {
    ClientId clientId;
    OrderId orderId;
    
    bool operator==(const OrderKey& other) const {
        return clientId == other.clientId && orderId == other.orderId;
    }
    
    bool operator!=(const OrderKey& other) const {
        return !(*this == other);
    }
};

// Hash function for OrderKey
struct OrderKeyHash {
    std::uint64_t operator()(const OrderKey& key) const {
        // FNV-1a hash
        std::uint64_t h = 14695981039346656037ULL;
        h ^= key.clientId;
        h *= 1099511628211ULL;
        h ^= key.orderId;
        h *= 1099511628211ULL;
        return h;
    }
};

template <typename Key, typename Value, typename Hash = OrderKeyHash>
class FixedRobinHoodMap {
public:
    struct Entry {
        Key key;
        Value value;
        std::uint8_t distance; // Distance from ideal position
        bool occupied;
        
        Entry() : distance(0), occupied(false) {}
    };
    
    explicit FixedRobinHoodMap(std::uint32_t capacity = 1024)
        : capacity_(capacity)
        , mask_(capacity - 1)
        , size_(0)
        , entries_(new Entry[capacity]())
    {
        // Capacity must be power of 2
        assert((capacity & (capacity - 1)) == 0);
    }
    
    ~FixedRobinHoodMap() {
        delete[] entries_;
    }
    
    // Non-copyable, movable
    FixedRobinHoodMap(const FixedRobinHoodMap&) = delete;
    FixedRobinHoodMap& operator=(const FixedRobinHoodMap&) = delete;
    
    FixedRobinHoodMap(FixedRobinHoodMap&& other) noexcept
        : capacity_(other.capacity_)
        , mask_(other.mask_)
        , size_(other.size_)
        , entries_(other.entries_)
    {
        other.entries_ = nullptr;
        other.size_ = 0;
    }
    
    // Insert or update a key-value pair
    // Returns false if map is full
    bool insert(const Key& key, const Value& value) {
        if (size_ >= capacity_) {
            return false;
        }
        
        std::uint64_t hash = Hash{}(key);
        std::uint32_t pos = hash & mask_;
        std::uint8_t distance = 0;
        
        Entry newEntry;
        newEntry.key = key;
        newEntry.value = value;
        newEntry.distance = 0;
        newEntry.occupied = true;
        
        // Robin Hood insertion: place entry at its ideal position,
        // displacing entries that have traveled less distance
        while (true) {
            Entry& current = entries_[pos];
            
            if (!current.occupied) {
                // Found empty slot
                newEntry.distance = distance;
                current = newEntry;
                ++size_;
                return true;
            }
            
            // Check for duplicate key
            if (current.key == key) {
                current.value = value;
                return true;
            }
            
            // Robin Hood: if current entry has traveled less, swap
            if (current.distance < distance) {
                Entry temp = current;
                newEntry.distance = distance;
                current = newEntry;
                newEntry = temp;
                distance = temp.distance;
            }
            
            ++pos;
            pos &= mask_;
            ++distance;
            
            // Prevent infinite loop (shouldn't happen if load factor is correct)
            if (distance > capacity_) {
                return false;
            }
        }
    }
    
    // Find a value by key
    std::optional<Value> find(const Key& key) const {
        std::uint64_t hash = Hash{}(key);
        std::uint32_t pos = hash & mask_;
        std::uint8_t distance = 0;
        
        while (true) {
            const Entry& current = entries_[pos];
            
            if (!current.occupied) {
                return std::nullopt;
            }
            
            if (current.distance < distance) {
                // Entry would have been placed here if it existed
                return std::nullopt;
            }
            
            if (current.key == key) {
                return current.value;
            }
            
            ++pos;
            pos &= mask_;
            ++distance;
        }
    }
    
    // Check if key exists
    bool contains(const Key& key) const {
        return find(key).has_value();
    }
    
    // Erase a key
    // Uses backward-shift deletion
    bool erase(const Key& key) {
        std::uint64_t hash = Hash{}(key);
        std::uint32_t pos = hash & mask_;
        std::uint8_t distance = 0;
        
        // Find the entry
        while (true) {
            Entry& current = entries_[pos];
            
            if (!current.occupied) {
                return false;
            }
            
            if (current.distance < distance) {
                return false;
            }
            
            if (current.key == key) {
                break;
            }
            
            ++pos;
            pos &= mask_;
            ++distance;
        }
        
        // Backward-shift deletion
        std::uint32_t next = (pos + 1) & mask_;
        
        while (entries_[next].occupied && entries_[next].distance > 0) {
            Entry& current = entries_[pos];
            current = entries_[next];
            current.distance -= 1;
            
            pos = next;
            next = (pos + 1) & mask_;
        }
        
        entries_[pos].occupied = false;
        entries_[pos].distance = 0;
        --size_;
        return true;
    }
    
    // Get or insert a value
    Value& operator[](const Key& key) {
        std::uint64_t hash = Hash{}(key);
        std::uint32_t pos = hash & mask_;
        std::uint8_t distance = 0;
        
        while (true) {
            Entry& current = entries_[pos];
            
            if (!current.occupied) {
                // Insert new entry
                current.key = key;
                current.value = Value{};
                current.distance = distance;
                current.occupied = true;
                ++size_;
                return current.value;
            }
            
            if (current.key == key) {
                return current.value;
            }
            
            ++pos;
            pos &= mask_;
            ++distance;
        }
    }
    
    // Capacity and size
    std::uint32_t capacity() const { return capacity_; }
    std::uint32_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    float loadFactor() const { return static_cast<float>(size_) / capacity_; }
    
    // Clear all entries
    void clear() {
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            entries_[i].occupied = false;
            entries_[i].distance = 0;
        }
        size_ = 0;
    }
    
    // Iterate over all entries
    template <typename Func>
    void forEach(Func&& func) const {
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            if (entries_[i].occupied) {
                func(entries_[i].key, entries_[i].value);
            }
        }
    }
    
    // Verify integrity (for testing)
    bool verifyIntegrity() const {
        std::uint32_t count = 0;
        
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            const Entry& entry = entries_[i];
            
            if (entry.occupied) {
                ++count;
                
                // Check distance
                std::uint64_t hash = Hash{}(entry.key);
                std::uint32_t idealPos = hash & mask_;
                std::uint32_t distance = (i - idealPos + capacity_) & mask_;
                
                if (entry.distance != distance) {
                    return false;
                }
            }
        }
        
        return count == size_;
    }

private:
    std::uint32_t capacity_;
    std::uint32_t mask_;
    std::uint32_t size_;
    Entry* entries_;
};

} // namespace lockstep
