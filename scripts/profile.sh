#!/bin/bash
set -e

echo "=== Profiling Lockstep ==="

make build

if [ "$(uname)" = "Linux" ]; then
    echo "Running perf stat..."
    perf stat -e cycles,instructions,ipc,branches,branch-misses,cache-references,cache-misses \
        ./build/lockstep_bench 2>&1 | tee artifacts/profile.txt
else
    echo "perf not supported on $(uname). Skipping."
    echo "{ \"supported\": false, \"reason\": \"perf only on Linux\", \"system\": \"$(uname)\" }" \
        > artifacts/profile_skip.json
fi

echo "Profiling complete."
