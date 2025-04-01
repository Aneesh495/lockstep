#!/bin/bash
set -e

echo "=== Configuring Lockstep ==="

cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release

echo "Configuration complete."
