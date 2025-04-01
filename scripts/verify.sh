#!/bin/bash
set -e

echo "=== Verifying Lockstep ==="

# Check formatting
if command -v clang-format &> /dev/null; then
    echo "Checking formatting..."
    find include src apps tests bench -name "*.cpp" -o -name "*.hpp" | \
        xargs clang-format --dry-run --Werror 2>/dev/null || {
        echo "Warning: clang-format check failed"
    }
fi

# Build
echo "Building..."
make build

# Run tests
echo "Running tests..."
cd build && ctest --output-on-failure

echo ""
echo "Verification complete."
