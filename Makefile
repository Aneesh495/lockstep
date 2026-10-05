# Makefile for Lockstep
# Primary build interface with CMake backend

.PHONY: all configure build clean test sanitize tsan fuzz-smoke demo benchmark profile stress acceptance verify help

SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c

# Default target
all: build

# Configuration
BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE ?= cmake
CTEST ?= ctest
CMAKE_FLAGS ?=
CLANG_FORMAT ?= clang-format

# Detect generator
ifeq ($(shell command -v ninja 2> /dev/null),)
    GENERATOR ?= "Unix Makefiles"
else
    GENERATOR ?= "Ninja"
endif

# Configure the project
configure:
	@echo "=== Configuring Lockstep ==="
	$(CMAKE) -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_FLAGS)

# Build the project
build: configure
	@echo "=== Building Lockstep ==="
	$(CMAKE) --build $(BUILD_DIR) --parallel

# Run tests
test: build
	@echo "=== Running Tests ==="
	cd $(BUILD_DIR) && $(CTEST) --output-on-failure --no-tests=error

# Run sanitizers (ASan + UBSan)
sanitize:
	@echo "=== Running AddressSanitizer and UBSan ==="
	$(CMAKE) -S . -B $(BUILD_DIR)-asan -G $(GENERATOR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DLOCKSTEP_ENABLE_SANITIZERS=ON \
		-DLOCKSTEP_BUILD_BENCHMARKS=OFF $(CMAKE_FLAGS)
	$(CMAKE) --build $(BUILD_DIR)-asan --parallel
	cd $(BUILD_DIR)-asan && $(CTEST) --output-on-failure --no-tests=error

# Run ThreadSanitizer
tsan:
	@echo "=== Running ThreadSanitizer ==="
	$(CMAKE) -S . -B $(BUILD_DIR)-tsan -G $(GENERATOR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DLOCKSTEP_ENABLE_TSAN=ON \
		-DLOCKSTEP_BUILD_BENCHMARKS=OFF $(CMAKE_FLAGS)
	$(CMAKE) --build $(BUILD_DIR)-tsan --parallel
	cd $(BUILD_DIR)-tsan && $(CTEST) --output-on-failure --no-tests=error -R "^(spsc|network|udp)$$"

# Run fuzzer smoke tests
fuzz-smoke:
	$(CMAKE) -S . -B $(BUILD_DIR)-fuzz -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Debug -DLOCKSTEP_BUILD_FUZZERS=ON -DLOCKSTEP_ENABLE_SANITIZERS=ON -DLOCKSTEP_BUILD_BENCHMARKS=OFF $(CMAKE_FLAGS)
	$(CMAKE) --build $(BUILD_DIR)-fuzz --parallel
	$(BUILD_DIR)-fuzz/fuzz_frame_decoder
	$(BUILD_DIR)-fuzz/fuzz_wal_decoder
	$(BUILD_DIR)-fuzz/fuzz_snapshot_decoder

# Run demo
demo: build
	@echo "=== Running Demo ==="
	@mkdir -p artifacts/demo
	$(BUILD_DIR)/lockstep_demo 2>&1 | tee artifacts/demo/transcript.txt

# Run benchmarks
benchmark: build
	@echo "=== Running Benchmarks ==="
	@mkdir -p artifacts/benchmarks/raw
	$(BUILD_DIR)/lockstep_bench --output artifacts/benchmarks/raw
	python3 tools/make_report.py --benchmark artifacts/benchmarks/raw

# Profile with perf (Linux only)
profile: build
	@echo "=== Profiling ==="
	@if [ "$(shell uname)" = "Linux" ]; then \
		echo "Running perf stat on benchmark..."; \
		perf stat -e cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches \
			$(BUILD_DIR)/lockstep_bench --repetitions 10 --operations 1000000 \
			2>&1 | tee artifacts/profile.txt; \
	else \
		echo "perf not supported on $(shell uname). Recording skip."; \
		echo "{ \"supported\": false, \"reason\": \"perf stat only available on Linux\", \"system\": \"$(shell uname)\" }" > artifacts/profile_skip.json; \
	fi

# Run stress tests
stress: build
	@echo "=== Running Stress Tests ==="
	@mkdir -p artifacts/stress/shards
	$(BUILD_DIR)/lockstep_fault_stress --output artifacts/stress
	$(BUILD_DIR)/lockstep_crash_matrix --output artifacts/stress

# Evidence generation runs on committed source and never writes prose.
acceptance:
	python3 tools/run_acceptance.py

verify:
	python3 tools/make_report.py --verify

format:
	python3 tools/check_headers.py --format-only --clang-format $(CLANG_FORMAT)

headers:
	python3 tools/check_headers.py --compiler $(CXX)

# Clean
clean:
	@echo "=== Cleaning ==="
	rm -rf $(BUILD_DIR) $(BUILD_DIR)-asan $(BUILD_DIR)-tsan $(BUILD_DIR)-fuzz $(BUILD_DIR)-release

# Help
help:
	@echo "Lockstep Build Commands"
	@echo "======================="
	@echo "  make configure     - Configure the project"
	@echo "  make build         - Build the project"
	@echo "  make test          - Run unit and integration tests"
	@echo "  make sanitize      - Run AddressSanitizer and UBSan tests"
	@echo "  make tsan          - Run ThreadSanitizer tests"
	@echo "  make fuzz-smoke    - Run fuzzer smoke tests"
	@echo "  make demo          - Run the demo scenario"
	@echo "  make benchmark     - Run benchmarks"
	@echo "  make profile       - Profile with perf (Linux only)"
	@echo "  make stress        - Run stress tests"
	@echo "  make acceptance    - Full acceptance suite"
	@echo "  make verify        - Verification only (no long tests)"
	@echo "  make clean         - Clean build artifacts"
	@echo ""
	@echo "Options:"
	@echo "  BUILD_DIR=<dir>    - Build directory (default: build)"
	@echo "  BUILD_TYPE=<type>  - Build type (default: Release)"
