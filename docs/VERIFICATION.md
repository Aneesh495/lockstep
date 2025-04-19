# Verification

## Test categories

### Unit tests

| File | Coverage |
|------|----------|
| test_endian.cpp | Byte order conversion |
| test_crc32c.cpp | CRC32C correctness |
| test_checked_math.cpp | Overflow detection |
| test_object_pool.cpp | Pool allocation |
| test_robin_hood_map.cpp | Hash table operations |
| test_spsc_ring.cpp | Ring buffer transfer |
| test_order_book.cpp | Book operations |
| test_matching_engine.cpp | Engine behavior |
| test_risk_engine.cpp | Risk limits |
| test_codec.cpp | Protocol encoding |
| test_wal.cpp | WAL read/write |
| test_snapshot.cpp | Snapshot format |

### Property tests

| File | Properties |
|------|------------|
| test_differential.cpp | Optimized vs reference book; digest equality |

### Integration tests

| File | Coverage |
|------|----------|
| test_network.cpp | TCP/UDP, dual-feed gaps |
| test_recovery.cpp | Crash recovery paths |

### Golden tests

| File | Coverage |
|------|----------|
| test_golden.cpp | CRC32C / SHA256 check vectors |

## Resilience gates (resume-facing)

These sit beside unit tests and are required for the dual-feed / WAL story:

| Gate | Requirement |
| --- | --- |
| Fault stress | **≥100,000,000** logical events processed under fault injection |
| Fault actions | Concrete drops / dupes / reorders / corruptions / outage suppressions across required profiles |
| Recovery matrix | **≥10,000** distinct recovery scenarios (snapshot + WAL prefix / kill / truncate) |
| Mismatches | **0** public L2 digest mismatches; **0** recovered L3 state digest mismatches |

Harnesses: `bench/lockstep_fault_stress.cpp`, `bench/lockstep_crash_matrix.cpp`.
Logical events count unique intended positions on the dual-feed → arbiter →
digest path (not duplicate packets or redundant-channel copies). Profiles
include loss, duplicate, reorder, corruption, and single/both-channel gap or
outage schedules (seeded, deterministic).

Performance gates (isolated core **≥5M commands/s**, **p99 &lt;1 µs**, zero
allocs) are documented in `docs/BENCHMARKS.md` and are measured separately from
this stress path.

## Invariant checking

1. Book is not crossed
2. Order counts match pool accounting
3. Price-level quantities match sum of orders
4. Order indices are consistent
5. Free list is correct
6. Sequences are monotonic

## Sanitizers

- **ASan** - overflows, UAF, leaks (`make sanitize`)
- **TSan** - data races (`make tsan`)
- **UBSan** - overflow / null / alignment (with sanitize)

## Fuzzing

| Target | Coverage |
|--------|----------|
| fuzz_frame_decoder.cpp | Frame parsing |
| fuzz_wal_decoder.cpp | WAL record parsing |
| fuzz_snapshot_decoder.cpp | Snapshot parsing |

`make fuzz-smoke`

## Commands

```bash
make test
make sanitize
make tsan
make fuzz-smoke
make benchmark
make stress          # aggregate 100M fault events + 10K recoveries
make verify
```
