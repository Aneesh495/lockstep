#!/bin/bash
set -e

echo "=== Lockstep Demo ==="

# Build if needed
if [ ! -f "build/lockstep_demo" ]; then
    make build
fi

# Run demo
./build/lockstep_demo

echo ""
echo "Demo complete. Artifacts in artifacts/demo/"
