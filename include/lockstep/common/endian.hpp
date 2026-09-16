#pragma once

#include <cstdint>
#include <cstring>
#include <array>

namespace lockstep {

// Network byte order (big-endian) conversion utilities
// All wire integers use network byte order

inline std::uint16_t hostToNetwork16(std::uint16_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(value);
#else
    return value;
#endif
}

inline std::uint32_t hostToNetwork32(std::uint32_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

inline std::uint64_t hostToNetwork64(std::uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(value);
#else
    return value;
#endif
}

inline std::uint16_t networkToHost16(std::uint16_t value) {
    return hostToNetwork16(value);
}

inline std::uint32_t networkToHost32(std::uint32_t value) {
    return hostToNetwork32(value);
}

inline std::uint64_t networkToHost64(std::uint64_t value) {
    return hostToNetwork64(value);
}

// Safe byte-level read/write for unaligned access
// Never use type-punning on packed structs

template <typename T>
T readUnaligned(const void* ptr) {
    T value;
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

template <typename T>
void writeUnaligned(void* ptr, T value) {
    std::memcpy(ptr, &value, sizeof(T));
}

// Read big-endian integer from bytes
inline std::uint16_t readBeU16(const void* ptr) {
    std::uint16_t networkValue;
    std::memcpy(&networkValue, ptr, sizeof(std::uint16_t));
    return networkToHost16(networkValue);
}

inline std::uint32_t readBeU32(const void* ptr) {
    std::uint32_t networkValue;
    std::memcpy(&networkValue, ptr, sizeof(std::uint32_t));
    return networkToHost32(networkValue);
}

inline std::uint64_t readBeU64(const void* ptr) {
    std::uint64_t networkValue;
    std::memcpy(&networkValue, ptr, sizeof(std::uint64_t));
    return networkToHost64(networkValue);
}

inline std::int16_t readBeI16(const void* ptr) {
    std::uint16_t u = readBeU16(ptr);
    return static_cast<std::int16_t>(u);
}

inline std::int32_t readBeI32(const void* ptr) {
    std::uint32_t u = readBeU32(ptr);
    return static_cast<std::int32_t>(u);
}

inline std::int64_t readBeI64(const void* ptr) {
    std::uint64_t u = readBeU64(ptr);
    return static_cast<std::int64_t>(u);
}

// Write big-endian integer to bytes
inline void writeBeU16(void* ptr, std::uint16_t value) {
    std::uint16_t networkValue = hostToNetwork16(value);
    std::memcpy(ptr, &networkValue, sizeof(std::uint16_t));
}

inline void writeBeU32(void* ptr, std::uint32_t value) {
    std::uint32_t networkValue = hostToNetwork32(value);
    std::memcpy(ptr, &networkValue, sizeof(std::uint32_t));
}

inline void writeBeU64(void* ptr, std::uint64_t value) {
    std::uint64_t networkValue = hostToNetwork64(value);
    std::memcpy(ptr, &networkValue, sizeof(std::uint64_t));
}

inline void writeBeI16(void* ptr, std::int16_t value) {
    writeBeU16(ptr, static_cast<std::uint16_t>(value));
}

inline void writeBeI32(void* ptr, std::int32_t value) {
    writeBeU32(ptr, static_cast<std::uint32_t>(value));
}

inline void writeBeI64(void* ptr, std::int64_t value) {
    writeBeU64(ptr, static_cast<std::uint64_t>(value));
}

// Byte-by-byte read for bounds checking
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size), pos_(0) {}
    
    bool readU8(std::uint8_t& out) {
        if (pos_ >= size_) return false;
        out = data_[pos_++];
        return true;
    }
    
    bool readU16(std::uint16_t& out) {
        if (pos_ + 2 > size_) return false;
        out = readBeU16(data_ + pos_);
        pos_ += 2;
        return true;
    }
    
    bool readU32(std::uint32_t& out) {
        if (pos_ + 4 > size_) return false;
        out = readBeU32(data_ + pos_);
        pos_ += 4;
        return true;
    }
    
    bool readU64(std::uint64_t& out) {
        if (pos_ + 8 > size_) return false;
        out = readBeU64(data_ + pos_);
        pos_ += 8;
        return true;
    }
    
    bool readI64(std::int64_t& out) {
        if (pos_ + 8 > size_) return false;
        out = readBeI64(data_ + pos_);
        pos_ += 8;
        return true;
    }
    
    bool readBytes(void* out, std::size_t len) {
        if (pos_ + len > size_) return false;
        std::memcpy(out, data_ + pos_, len);
        pos_ += len;
        return true;
    }
    
    bool skip(std::size_t len) {
        if (pos_ + len > size_) return false;
        pos_ += len;
        return true;
    }
    
    std::size_t pos() const { return pos_; }
    std::size_t remaining() const { return size_ - pos_; }
    const std::uint8_t* data() const { return data_; }
    
private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_;
};

// Byte-by-byte write
class ByteWriter {
public:
    ByteWriter(std::uint8_t* data, std::size_t size)
        : data_(data), size_(size), pos_(0) {}
    
    bool writeU8(std::uint8_t value) {
        if (pos_ >= size_) return false;
        data_[pos_++] = value;
        return true;
    }
    
    bool writeU16(std::uint16_t value) {
        if (pos_ + 2 > size_) return false;
        writeBeU16(data_ + pos_, value);
        pos_ += 2;
        return true;
    }
    
    bool writeU32(std::uint32_t value) {
        if (pos_ + 4 > size_) return false;
        writeBeU32(data_ + pos_, value);
        pos_ += 4;
        return true;
    }
    
    bool writeU64(std::uint64_t value) {
        if (pos_ + 8 > size_) return false;
        writeBeU64(data_ + pos_, value);
        pos_ += 8;
        return true;
    }
    
    bool writeI64(std::int64_t value) {
        if (pos_ + 8 > size_) return false;
        writeBeI64(data_ + pos_, value);
        pos_ += 8;
        return true;
    }
    
    bool writeBytes(const void* value, std::size_t len) {
        if (pos_ + len > size_) return false;
        std::memcpy(data_ + pos_, value, len);
        pos_ += len;
        return true;
    }
    
    bool skip(std::size_t len) {
        if (pos_ + len > size_) return false;
        pos_ += len;
        return true;
    }
    
    std::size_t pos() const { return pos_; }
    std::size_t remaining() const { return size_ - pos_; }
    std::uint8_t* data() { return data_; }
    
private:
    std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_;
};

} // namespace lockstep
