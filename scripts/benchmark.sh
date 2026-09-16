#!/bin/bash
set -e

echo "=== Lockstep Benchmark ==="

# Build if needed
make build

# Run benchmark
./build/lockstep_bench

echo ""
echo "Benchmark complete. Results in artifacts/benchmarks/raw/"
