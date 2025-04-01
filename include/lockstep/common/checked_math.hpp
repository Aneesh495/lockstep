#pragma once

#include "lockstep/common/types.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace lockstep {

// Checked arithmetic for overflow-safe calculations
// Uses __int128 for intermediate notional calculations

template <typename T>
constexpr std::optional<T> checkedAdd(T a, T b) {
    static_assert(std::is_integral_v<T>);
    if constexpr (std::is_signed_v<T>) {
        if (b > 0 && a > std::numeric_limits<T>::max() - b) {
            return std::nullopt;
        }
        if (b < 0 && a < std::numeric_limits<T>::min() - b) {
            return std::nullopt;
        }
    } else {
        if (a > std::numeric_limits<T>::max() - b) {
            return std::nullopt;
        }
    }
    return a + b;
}

template <typename T>
constexpr std::optional<T> checkedSub(T a, T b) {
    static_assert(std::is_integral_v<T>);
    if constexpr (std::is_signed_v<T>) {
        if (b > 0 && a < std::numeric_limits<T>::min() + b) {
            return std::nullopt;
        }
        if (b < 0 && a > std::numeric_limits<T>::max() + b) {
            return std::nullopt;
        }
    } else {
        if (a < b) {
            return std::nullopt;
        }
    }
    return a - b;
}

template <typename T>
constexpr std::optional<T> checkedMul(T a, T b) {
    static_assert(std::is_integral_v<T>);
    if (a == 0 || b == 0) {
        return T{0};
    }
    if constexpr (std::is_signed_v<T>) {
        if (a > 0) {
            if (b > 0) {
                if (a > std::numeric_limits<T>::max() / b) {
                    return std::nullopt;
                }
            } else {
                if (b < std::numeric_limits<T>::min() / a) {
                    return std::nullopt;
                }
            }
        } else {
            if (b > 0) {
                if (a < std::numeric_limits<T>::min() / b) {
                    return std::nullopt;
                }
            } else {
                if (a < std::numeric_limits<T>::max() / b) {
                    return std::nullopt;
                }
            }
        }
    } else {
        if (a > std::numeric_limits<T>::max() / b) {
            return std::nullopt;
        }
    }
    return a * b;
}

template <typename T>
constexpr std::optional<T> checkedDiv(T a, T b) {
    static_assert(std::is_integral_v<T>);
    if (b == 0) {
        return std::nullopt;
    }
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == -1) {
            return std::nullopt;
        }
    }
    return a / b;
}

// Signed 64-bit position with unsigned 64-bit price
// Returns signed 128-bit notional
inline std::optional<Notional> computeNotional(Price price, Quantity quantity) {
    // Price is signed, quantity is unsigned
    // Result is signed 128-bit
    Notional p = static_cast<Notional>(price);
    Notional q = static_cast<Notional>(quantity);
    return p * q; // __int128 multiplication cannot overflow from i64 * u32
}

// Signed position + signed delta
inline std::optional<Position> updatePosition(Position current, Position delta) {
    return checkedAdd(current, delta);
}

// Check if adding quantity would overflow
inline bool wouldOverflowAdd(std::uint32_t a, std::uint32_t b) {
    return a > std::numeric_limits<std::uint32_t>::max() - b;
}

// Check if subtracting quantity would underflow
inline bool wouldUnderflowSub(std::uint32_t a, std::uint32_t b) {
    return a < b;
}

// Absolute value of position (returns positive quantity)
inline std::uint64_t absPosition(Position pos) {
    if (pos >= 0) {
        return static_cast<std::uint64_t>(pos);
    }
    // Handle minimum value carefully
    if (pos == std::numeric_limits<Position>::min()) {
        return static_cast<std::uint64_t>(std::numeric_limits<Position>::max()) + 1;
    }
    return static_cast<std::uint64_t>(-pos);
}

// Compare notional values
inline bool notionalExceeds(Notional value, Notional limit) {
    return value > limit;
}

// Clamp quantity to valid range
inline Quantity clampQuantity(std::uint64_t value) {
    if (value > std::numeric_limits<Quantity>::max()) {
        return std::numeric_limits<Quantity>::max();
    }
    return static_cast<Quantity>(value);
}

// Safe cast from 128-bit to 64-bit (for reporting)
inline std::optional<std::int64_t> notionalToInt64(Notional value) {
    if (value > std::numeric_limits<std::int64_t>::max() ||
        value < std::numeric_limits<std::int64_t>::min()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}

} // namespace lockstep
