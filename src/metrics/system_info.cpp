#include "lockstep/metrics/system_info.hpp"
#include <thread>
#include <chrono>
#include <ctime>
#include <cstring>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

namespace lockstep {

SystemInfo SystemInfo::collect() {
    SystemInfo info;
    
    // Compiler info
#if defined(__clang__)
    info.compiler = "clang";
    info.compilerVersion = std::to_string(__clang_major__) + "." + 
                           std::to_string(__clang_minor__) + "." +
                           std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    info.compiler = "gcc";
    info.compilerVersion = std::to_string(__GNUC__) + "." +
                           std::to_string(__GNUC_MINOR__) + "." +
                           std::to_string(__GNUC_PATCHLEVEL__);
#else
    info.compiler = "unknown";
    info.compilerVersion = "unknown";
#endif
    
    // OS info
#ifdef __APPLE__
    info.os = "macOS";
    
    char buf[256];
    size_t size = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &size, nullptr, 0) == 0) {
        info.cpuModel = buf;
    }
    
    size = sizeof(info.cpuCores);
    sysctlbyname("hw.physicalcpu", &info.cpuCores, &size, nullptr, 0);
    
    size = sizeof(info.totalMemory);
    sysctlbyname("hw.memsize", &info.totalMemory, &size, nullptr, 0);
    
#elif defined(__linux__)
    info.os = "Linux";
    
    struct utsname uts;
    if (uname(&uts) == 0) {
        info.kernel = uts.release;
        info.cpuArch = uts.machine;
    }
    
    info.cpuCores = std::thread::hardware_concurrency();
    
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info.totalMemory = si.totalram * si.mem_unit;
    }
#else
    info.os = "unknown";
#endif
    
    info.cpuArch = 
#if defined(__aarch64__) || defined(__arm64__)
        "arm64";
#elif defined(__x86_64__) || defined(__amd64__)
        "x86_64";
#else
        "unknown";
#endif
    
    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char ts[64];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    info.timestamp = ts;
    
    return info;
}

} // namespace lockstep
