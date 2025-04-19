# Benchmarks

## Isolation rules (read first)

Resume performance claims are for the **matching core only**:

- **Included:** order book + matching engine (+ risk when the workload enables it)
- **Excluded:** TCP/UDP syscalls, kernel networking, WAL append/fsync, snapshots

Throughput and latency are **separate tests**. Never quote a latency percentile
from a bulk throughput loop, or a commands/s number from the latency harness.

## Resume gates

| Gate | Threshold | Harness notes |
| --- | --- | --- |
| Throughput | **≥5,000,000 commands/s** | Pre-generated commands; warm-up; median over repetitions |
| Latency | **p99 &lt;1,000 ns (1 µs)** | One command at a time; raw (unadjusted) p99; nearest-rank |
| Allocations | **0** heap allocs after init | `AllocationCounter` over the measured window |

Passing above the gate (e.g. ~29M ops/s median on Apple Silicon) is expected and
may be reported as an observed median, but documentation and resume language use
the **5M+ / &lt;1 µs** thresholds.

## Throughput method

1. Pre-generate all commands
2. Warm up (≥2M operations)
3. Measure a large batch per repetition (≥10M ops when runtime allows)
4. Run ≥10 repetitions; report median commands/s

## Latency method

1. Pre-generate commands
2. Timestamp before / after each operation (steady / cycle clock)
3. Store raw samples (preallocated); ≥1M samples per repetition when gated
4. Percentiles via nearest-rank over the full sample set: p50, p95, p99, p99.9
5. Run multiple repetitions; resume uses **raw p99** (timer overhead disclosed,
   not subtracted for the gate)

## Workload mix

| Command | Percentage |
|---------|------------|
| New resting GTC | 40% |
| Cancel | 20% |
| Replace | 15% |
| Marketable limit | 15% |
| IOC | 5% |
| FOK | 5% |

Price range: 100-200 ticks. Quantity range: 10-109 lots.

## Evidence

Raw runs land under `artifacts/benchmarks/`. Verified summaries under
`results/verified/` (see `docs/RESUME.md`).

```bash
make benchmark
```

## Notes

- Zero-allocation verification is part of the bench / acceptance path
- Reference-book differential checks are correctness, not throughput
- Networked or durable end-to-end paths are profiled separately and are **not**
  the 5M+/s or &lt;1 µs claims
