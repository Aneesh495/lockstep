# Benchmarks

## Methodology

### Throughput Measurement

Throughput is measured in a tight loop:

1. Pre-generate all commands
2. Warm up with 2M operations
3. Measure 10M operations per repetition
4. Run 10 repetitions

### Latency Measurement

Latency is measured per operation:

1. Pre-generate commands
2. Record timestamp before each operation
3. Record timestamp after each operation
4. Compute percentiles over raw samples

### Percentile Calculation

Uses nearest-rank method over complete sample set:

- p50: median
- p95: 95th percentile
- p99: 99th percentile
- p99.9: 99.9th percentile

## Workload

Command distribution:

| Command | Percentage |
|---------|------------|
| New resting GTC | 40% |
| Cancel | 20% |
| Replace | 15% |
| Marketable limit | 15% |
| IOC | 5% |
| FOK | 5% |

Price range: 100-200 ticks
Quantity range: 10-109 lots

## Results

Results are generated from actual benchmark runs and stored in `artifacts/benchmarks/raw/`.

See `results/verified/BENCHMARKS.json` for final verified results.

## System Requirements

For reproducible results:

- Dedicated machine (no other load)
- Pinned CPU core (Linux only)
- Disabled power management
- Consistent memory configuration

## Commands

```bash
# Run benchmarks
make benchmark

# Run with perf (Linux)
make profile
```

## Notes

- Core measurements exclude network I/O
- Core measurements exclude WAL I/O
- Zero-allocation verification included
- Reference book comparison included
