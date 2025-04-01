#!/bin/bash
set -e

echo "=== Lockstep Acceptance Suite ==="

# Full acceptance run
make acceptance

echo ""
echo "Acceptance complete. Results in results/verified/"
