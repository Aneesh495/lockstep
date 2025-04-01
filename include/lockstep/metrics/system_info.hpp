#pragma once

#include <cstdint>
#include <string>

namespace lockstep {

// System information for reproducibility

struct SystemInfo {
    std::string compiler;
    std::string compilerVersion;
    std::string flags;
    
    std::string os;
    std::string kernel;
    std::string cpuModel;
    std::string cpuArch;
    std::uint32_t cpuCores = 0;
    
    std::uint64_t totalMemory = 0;
    
    std::string timestamp;
    
    static SystemInfo collect();
};

} // namespace lockstep
