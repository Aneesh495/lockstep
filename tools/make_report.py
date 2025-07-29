#!/usr/bin/env python3
"""
Verification and report generation for Lockstep exchange engine.
"""

import argparse
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def verify_reports(root=ROOT):
    acc_path = root / "results/verified/ACCEPTANCE.json"
    if not acc_path.exists():
        print(f"FAIL: Missing {acc_path}", file=sys.stderr)
        return 1

    try:
        with open(acc_path, "r") as f:
            acc = json.load(f)
    except Exception as e:
        print(f"FAIL: Failed to parse {acc_path}: {e}", file=sys.stderr)
        return 1

    if acc.get("status") != "PASSED":
        print(f"FAIL: Acceptance status is not PASSED: {acc.get('status')}", file=sys.stderr)
        return 1

    perf = acc.get("performance", {})
    if perf.get("hot_path_allocations", -1) != 0:
        print("FAIL: Hot path allocations must be 0", file=sys.stderr)
        return 1

    if perf.get("median_commands_per_second", 0) < perf.get("throughput_gate_commands_per_sec", 5000000):
        print("FAIL: Throughput gate not met", file=sys.stderr)
        return 1

    res = acc.get("resilience", {})
    if res.get("state_mismatches", -1) != 0:
        print("FAIL: State mismatches must be 0", file=sys.stderr)
        return 1

    print("Lockstep acceptance verification passed:")
    print(f"  Platform:    {acc.get('platform', 'unknown')}")
    print(f"  Throughput:  {perf.get('median_commands_per_second', 0):,} commands/s (gate: {perf.get('throughput_gate_commands_per_sec', 0):,})")
    print(f"  p99 Latency: {perf.get('median_p99_latency_ns', 0)} ns (gate: {perf.get('p99_latency_gate_ns', 0)} ns)")
    print(f"  Resilience:  {res.get('logical_events', 0):,} events, {res.get('recovery_scenarios', 0):,} recoveries, {res.get('state_mismatches', 0)} mismatches")
    return 0


def generate_benchmark_summary(input_dir, output_file):
    input_path = Path(input_dir) / "benchmark.json"
    if not input_path.exists():
        print(f"Warning: {input_path} not found")
        return

    with open(input_path, "r") as f:
        data = json.load(f)

    with open(output_file, "w") as f:
        json.dump(data, f, indent=2)
    print(f"Wrote benchmark summary to {output_file}")


def main():
    parser = argparse.ArgumentParser(description="Lockstep acceptance and report generator")
    parser.add_argument("--verify", action="store_true", help="Verify acceptance evidence")
    parser.add_argument("--benchmark", type=Path, help="Benchmark output directory")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()

    if args.verify:
        return verify_reports(args.root)
    elif args.benchmark:
        generate_benchmark_summary(args.benchmark, args.benchmark / "summary.json")
        return 0
    else:
        parser.print_help()
        return 1


if __name__ == "__main__":
    sys.exit(main())
