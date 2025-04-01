# Makefile for Lockstep
# Primary build interface with CMake backend

.PHONY: all configure build clean test sanitize tsan fuzz-smoke demo benchmark profile stress acceptance verify help

# Default target
all: build

# Configuration
BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE ?= cmake
CTEST ?= ctest

# Detect generator
ifeq ($(shell command -v ninja 2> /dev/null),)
    GENERATOR ?= "Unix Makefiles"
else
    GENERATOR ?= "Ninja"
endif

# Configure the project
configure:
	@echo "=== Configuring Lockstep ==="
	$(CMAKE) -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

# Build the project
build: configure
	@echo "=== Building Lockstep ==="
	$(CMAKE) --build $(BUILD_DIR) --parallel

# Run tests
test: build
	@echo "=== Running Tests ==="
	cd $(BUILD_DIR) && $(CTEST) --output-on-failure

# Run sanitizers (ASan + UBSan)
sanitize:
	@echo "=== Running AddressSanitizer and UBSan ==="
	$(CMAKE) -S . -B $(BUILD_DIR)-asan -G $(GENERATOR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DLOCKSTEP_ENABLE_SANITIZERS=ON \
		-DLOCKSTEP_BUILD_BENCHMARKS=OFF
	$(CMAKE) --build $(BUILD_DIR)-asan --parallel
	cd $(BUILD_DIR)-asan && $(CTEST) --output-on-failure

# Run ThreadSanitizer
tsan:
	@echo "=== Running ThreadSanitizer ==="
	$(CMAKE) -S . -B $(BUILD_DIR)-tsan -G $(GENERATOR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DLOCKSTEP_ENABLE_TSAN=ON \
		-DLOCKSTEP_BUILD_BENCHMARKS=OFF
	$(CMAKE) --build $(BUILD_DIR)-tsan --parallel
	cd $(BUILD_DIR)-tsan && $(CTEST) --output-on-failure -R "spsc|network"

# Run fuzzer smoke tests
fuzz-smoke:
	@echo "=== Running Fuzzer Smoke Tests ==="
	@if [ ! -d "$(BUILD_DIR)-fuzz" ]; then \
		$(CMAKE) -S . -B $(BUILD_DIR)-fuzz -G $(GENERATOR) \
			-DCMAKE_BUILD_TYPE=Debug \
			-DLOCKSTEP_BUILD_FUZZERS=ON \
			-DLOCKSTEP_BUILD_BENCHMARKS=OFF; \
	fi
	$(CMAKE) --build $(BUILD_DIR)-fuzz --parallel
	@echo "Running fuzz_frame_decoder for 10 seconds..."
	@timeout 10s $(BUILD_DIR)-fuzz/fuzz_frame_decoder -max_total_time=10 -runs=1000 || true
	@echo "Running fuzz_wal_decoder for 10 seconds..."
	@timeout 10s $(BUILD_DIR)-fuzz/fuzz_wal_decoder -max_total_time=10 -runs=1000 || true
	@echo "Running fuzz_snapshot_decoder for 10 seconds..."
	@timeout 10s $(BUILD_DIR)-fuzz/fuzz_snapshot_decoder -max_total_time=10 -runs=1000 || true
	@echo "Fuzzer smoke tests complete"

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
	python3 tools/make_report.py --input artifacts/benchmarks/raw --output docs/BENCHMARKS.md

# Profile with perf (Linux only)
profile: build
	@echo "=== Profiling ==="
	@if [ "$(shell uname)" = "Linux" ]; then \
		echo "Running perf stat on benchmark..."; \
		perf stat -e cycles,instructions,ipc,branches,branch-misses,cache-references,cache-misses,context-switches \
			$(BUILD_DIR)/lockstep_bench --mode throughput --repetitions 1 --operations 1000000 \
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

# Full acceptance run
acceptance:
	@echo "=== Running Full Acceptance Suite ==="
	@mkdir -p artifacts/acceptance artifacts/benchmarks/raw artifacts/stress/shards
	@echo "Step 1: Clean build"
	$(CMAKE) -S . -B $(BUILD_DIR)-release -G $(GENERATOR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DLOCKSTEP_ENABLE_NATIVE=ON
	$(CMAKE) --build $(BUILD_DIR)-release --parallel
	@echo "Step 2: Run tests"
	cd $(BUILD_DIR)-release && $(CTEST) --output-on-failure
	@echo "Step 3: Run sanitizers"
	$(MAKE) sanitize
	@echo "Step 4: Run fuzzer smoke tests"
	$(MAKE) fuzz-smoke
	@echo "Step 5: Run demo"
	$(MAKE) demo
	@echo "Step 6: Run profiling"
	$(MAKE) profile
	@echo "Step 7: Run benchmarks"
	$(MAKE) benchmark
	@echo "Step 8: Run stress tests"
	$(MAKE) stress
	@echo "Step 9: Generate acceptance artifacts"
	python3 tools/make_report.py --acceptance
	@echo "=== Acceptance Complete ==="

# Verification only (no long-running tests)
verify:
	@echo "=== Verifying Lockstep ==="
	@echo "Checking formatting..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find include src apps tests bench -name "*.cpp" -o -name "*.hpp" | \
		xargs clang-format --dry-run --Werror || exit 1; \
	else \
		echo "clang-format not found, skipping format check"; \
	fi
	@echo "Checking build..."
	$(MAKE) build
	@echo "Running quick tests..."
	cd $(BUILD_DIR) && $(CTEST) --output-on-failure -E "stress|benchmark"
	@echo "Checking evidence hashes..."
	@if [ -f results/verified/RESUME_METRICS.json ]; then \
		python3 tools/make_report.py --verify; \
	else \
		echo "No evidence to verify yet"; \
	fi
	@echo "=== Verification Complete ==="

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
