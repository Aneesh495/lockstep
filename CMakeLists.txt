cmake_minimum_required(VERSION 3.24)

project(Lockstep
    VERSION 1.0.0
    DESCRIPTION "Low-latency deterministic electronic exchange and recovery engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CheckCXXCompilerFlag)

# Detect native CPU flags for optimized builds
set(NATIVE_CPU_FLAG "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    check_cxx_compiler_flag(-march=native HAVE_MARCH_NATIVE)
    if(HAVE_MARCH_NATIVE)
        set(NATIVE_CPU_FLAG "-march=native")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    check_cxx_compiler_flag(-mcpu=native HAVE_MCPU_NATIVE)
    if(HAVE_MCPU_NATIVE)
        set(NATIVE_CPU_FLAG "-mcpu=native")
    endif()
endif()

include(CompilerWarnings)
include(Sanitizers)

# Build types
set(DEFAULT_BUILD_TYPE Release)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE ${DEFAULT_BUILD_TYPE} CACHE STRING "Build type" FORCE)
endif()

# Options
option(LOCKSTEP_BUILD_TESTS "Build test suite" ON)
option(LOCKSTEP_BUILD_BENCHMARKS "Build benchmarks" ON)
option(LOCKSTEP_BUILD_APPS "Build applications" ON)
option(LOCKSTEP_ENABLE_SANITIZERS "Enable sanitizers" OFF)
option(LOCKSTEP_ENABLE_NATIVE "Enable native CPU optimizations" OFF)
option(LOCKSTEP_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Main library sources
set(LOCKSTEP_SOURCES
    src/common/crc32c.cpp
    src/common/stable_digest.cpp
    src/common/virtual_clock.cpp
    src/protocol/codec.cpp
    src/engine/order_book.cpp
    src/engine/reference_book.cpp
    src/engine/matching_engine.cpp
    src/risk/risk_engine.cpp
    src/persistence/wal.cpp
    src/persistence/snapshot.cpp
    src/persistence/recovery.cpp
    src/network/tcp_gateway.cpp
    src/network/udp_publisher.cpp
    src/network/feed_arbiter.cpp
    src/network/snapshot_client.cpp
    src/fault/fault_proxy.cpp
    src/metrics/histogram.cpp
    src/metrics/allocation_counter.cpp
    src/metrics/system_info.cpp
)

# Main library
add_library(lockstep_core STATIC ${LOCKSTEP_SOURCES})

target_include_directories(lockstep_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_warnings(lockstep_core)

if(LOCKSTEP_ENABLE_NATIVE AND NATIVE_CPU_FLAG)
    target_compile_options(lockstep_core PRIVATE ${NATIVE_CPU_FLAG})
endif()

if(LOCKSTEP_ENABLE_SANITIZERS)
    set_target_sanitizer(lockstep_core address)
endif()

if(LOCKSTEP_ENABLE_TSAN)
    set_target_sanitizer(lockstep_core thread)
endif()

# Optimized release flags
target_compile_options(lockstep_core PRIVATE
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
)

# Applications
if(LOCKSTEP_BUILD_APPS)
    add_executable(lockstep_exchange apps/lockstep_exchange.cpp)
    target_link_libraries(lockstep_exchange PRIVATE lockstep_core)
    set_target_warnings(lockstep_exchange)
    
    add_executable(lockstep_client apps/lockstep_client.cpp)
    target_link_libraries(lockstep_client PRIVATE lockstep_core)
    set_target_warnings(lockstep_client)
    
    add_executable(lockstep_demo apps/lockstep_demo.cpp)
    target_link_libraries(lockstep_demo PRIVATE lockstep_core)
    set_target_warnings(lockstep_demo)
    
    add_executable(lockstep_replay apps/lockstep_replay.cpp)
    target_link_libraries(lockstep_replay PRIVATE lockstep_core)
    set_target_warnings(lockstep_replay)
endif()

# Tests
if(LOCKSTEP_BUILD_TESTS)
    enable_testing()
    
    # Test sources
    set(TEST_SOURCES
        tests/test_main.cpp
        tests/test_runners.cpp
        tests/unit/test_endian.cpp
        tests/unit/test_crc32c.cpp
        tests/unit/test_checked_math.cpp
        tests/unit/test_object_pool.cpp
        tests/unit/test_robin_hood_map.cpp
        tests/unit/test_spsc_ring.cpp
        tests/unit/test_order_book.cpp
        tests/unit/test_order_book_detail.cpp
        tests/unit/test_matching_engine.cpp
        tests/unit/test_risk_engine.cpp
        tests/unit/test_codec.cpp
        tests/unit/test_wal.cpp
        tests/unit/test_snapshot.cpp
        tests/property/test_differential.cpp
        tests/integration/test_network.cpp
        tests/recovery/test_recovery.cpp
        tests/network/test_udp.cpp
        tests/golden/test_golden.cpp
    )
    
    add_executable(lockstep_tests ${TEST_SOURCES})
    target_link_libraries(lockstep_tests PRIVATE lockstep_core)
    set_target_warnings(lockstep_tests)
    
    if(LOCKSTEP_ENABLE_SANITIZERS)
        set_target_sanitizer(lockstep_tests address)
    endif()
    
    if(LOCKSTEP_ENABLE_TSAN)
        set_target_sanitizer(lockstep_tests thread)
    endif()
    
    # Register tests with CTest
    add_test(NAME LockstepTests COMMAND lockstep_tests)
endif()

# Benchmarks
if(LOCKSTEP_BUILD_BENCHMARKS)
    add_executable(lockstep_bench bench/lockstep_bench.cpp)
    target_link_libraries(lockstep_bench PRIVATE lockstep_core)
    set_target_warnings(lockstep_bench)
    
    add_executable(lockstep_fault_stress bench/lockstep_fault_stress.cpp)
    target_link_libraries(lockstep_fault_stress PRIVATE lockstep_core)
    set_target_warnings(lockstep_fault_stress)
    
    add_executable(lockstep_crash_matrix bench/lockstep_crash_matrix.cpp)
    target_link_libraries(lockstep_crash_matrix PRIVATE lockstep_core)
    set_target_warnings(lockstep_crash_matrix)
    
    if(LOCKSTEP_ENABLE_NATIVE AND NATIVE_CPU_FLAG)
        target_compile_options(lockstep_bench PRIVATE ${NATIVE_CPU_FLAG})
        target_compile_options(lockstep_fault_stress PRIVATE ${NATIVE_CPU_FLAG})
        target_compile_options(lockstep_crash_matrix PRIVATE ${NATIVE_CPU_FLAG})
    endif()
endif()

# Fuzz targets (standalone smoke test mode or libFuzzer mode)
option(LOCKSTEP_BUILD_FUZZERS "Build fuzzer targets (standalone smoke tests)" ON)

if(LOCKSTEP_BUILD_FUZZERS AND LOCKSTEP_BUILD_TESTS)
    # Standalone fuzz targets (no libFuzzer, just smoke tests)
    add_executable(fuzz_frame_decoder fuzz/fuzz_frame_decoder.cpp)
    target_link_libraries(fuzz_frame_decoder PRIVATE lockstep_core)
    set_target_warnings(fuzz_frame_decoder)
    
    add_executable(fuzz_wal_decoder fuzz/fuzz_wal_decoder.cpp)
    target_link_libraries(fuzz_wal_decoder PRIVATE lockstep_core)
    set_target_warnings(fuzz_wal_decoder)
    
    add_executable(fuzz_snapshot_decoder fuzz/fuzz_snapshot_decoder.cpp)
    target_link_libraries(fuzz_snapshot_decoder PRIVATE lockstep_core)
    set_target_warnings(fuzz_snapshot_decoder)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Lockstep Configuration Summary")
message(STATUS "================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Native CPU flag: ${NATIVE_CPU_FLAG}")
message(STATUS "Build tests: ${LOCKSTEP_BUILD_TESTS}")
message(STATUS "Build benchmarks: ${LOCKSTEP_BUILD_BENCHMARKS}")
message(STATUS "Build apps: ${LOCKSTEP_BUILD_APPS}")
message(STATUS "Enable sanitizers: ${LOCKSTEP_ENABLE_SANITIZERS}")
message(STATUS "Enable TSan: ${LOCKSTEP_ENABLE_TSAN}")
message(STATUS "Enable native: ${LOCKSTEP_ENABLE_NATIVE}")
message(STATUS "")
