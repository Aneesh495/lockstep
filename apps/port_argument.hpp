#pragma once

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace lockstep {

inline std::uint16_t parseNetworkPort(const std::string& value) {
    auto* end = value.data() + value.size();
    std::uint16_t port = 0;
    auto [parsed, error] = std::from_chars(value.data(), end, port);
    if (error != std::errc{} || parsed != end)
        throw std::invalid_argument("Invalid port: " + value);
    return port;
}

}  // namespace lockstep
