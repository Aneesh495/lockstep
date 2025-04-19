# Architecture

## Overview

Lockstep is a C++20 **deterministic exchange matching and recovery engine**:
price-time priority matching, pre-trade risk, TCP order entry, **dual UDP**
market-data feeds, and **WAL + snapshot** durability.

Correctness and determinism come first. Given the same command sequence, the
core produces identical books and digests. Performance claims below are for the
**isolated matching core** (no sockets, no disk) unless stated otherwise.

## Components

### Matching engine

`engine/matching_engine.hpp` - single-threaded deterministic apply path:

- Single writer: all book mutations on one engine thread
- Price-time priority: best price first, FIFO within level
- Fixed-capacity structures: **no heap allocations after init**
- Reference book for differential testing

### Order book

`engine/order_book.hpp`:

- Preallocated price-level arrays indexed by tick offset
- Bitset-assisted best bid/ask tracking
- Object pool for orders (intrusive free list)
- Robin Hood hash for order id lookup

### Risk engine

`risk/risk_engine.hpp` - per-client position / size / count limits, reservation
accounting, kill switch. Runs before the book so rejected orders never mutate
liquidity.

### Persistence

- Write-ahead log with CRC32C (`WALK` magic)
- Atomic snapshots (`SNAP` magic)
- Recovery: newest valid snapshot + WAL replay

### Networking

- TCP gateway for order entry
- **Dual UDP publishers** (Feed A / Feed B) with independent sequences
- Feed arbiter for gap detect and alternate-channel heal
- Seeded **fault proxy** for loss, duplicate, reorder, corruption, outages

## Threading model

```
                    +-----------------+
                    | TCP Gateway     |
                    | (parse thread)  |
                    +--------+--------+
                             |
                             v
                    +-----------------+
                    | SPSC ring       |
                    | (commands)      |
                    +--------+--------+
                             |
                             v
+----------+        +-----------------+        +----------+
| UDP A    |<-------| Matching Engine |------->| UDP B    |
+----------+        | (engine thread) |        +----------+
                    +--------+--------+
                             |
                             v
                    +-----------------+
                    | SPSC ring       |
                    | (responses)     |
                    +--------+--------+
                             |
                             v
                    +-----------------+
                    | TCP Gateway     |
                    | (send thread)   |
                    +-----------------+
```

WAL append sits on the durable path before ack when strict durability is on.
Isolated benches bypass TCP/UDP/WAL to measure the match core alone.

## Memory management

Hot-path structures are preallocated: price levels, order pool, order index,
SPSC rings. **Zero heap allocations after initialization** on the matching hot
path. `AllocationCounter` verifies this in instrumented runs.

## Determinism

- Same WAL command sequence → same state digest
- Virtual clock for engine timestamps during replay
- Stable hashing for digests
- No wall-clock reads on the match path

Verified by `test_differential` (optimized book vs `ReferenceBook`).

## Failure model

| Fault | Handling |
| --- | --- |
| Process kill | Snapshot + WAL replay |
| Single UDP channel loss / gap | Dual-feed arbiter + alternate channel |
| WAL truncated tail | Ignore incomplete last record |
| Mid-WAL corruption | Fail closed |

Not claimed: sudden power loss past controller cache, silent disk firmware bugs,
kernel panic survival.

## Performance characteristics (isolated core)

Resume **gates** (must hold on Release Apple Silicon / documented CI hosts):

| Metric | Gate |
| --- | --- |
| Throughput | **≥5M commands/s** (bulk loop, pre-generated commands) |
| Latency p99 | **&lt;1 µs** (separate one-at-a-time harness, ≥1M samples/rep) |
| Allocations | **0** after init |

Illustrative observed medians on Apple Silicon M-series (same isolation; not
the gate):

| Operation | Latency (p50) | Latency (p99) |
|-----------|---------------|---------------|
| New order (no match) | ~50 ns | ~200 ns |
| New order (with match) | ~100 ns | ~500 ns |
| Cancel | ~30 ns | ~100 ns |
| Replace | ~80 ns | ~300 ns |

Bulk throughput medians often land ~20-30M ops/s; always report the **5M+**
gate when summarizing for resume. Full method: `docs/BENCHMARKS.md`.

## Resilience characteristics

| Metric | Gate |
| --- | --- |
| Fault-injected logical events | **100M** across required fault profiles |
| Recovery trials | **10K** distinct snapshot/WAL scenarios |
| State digest mismatches | **0** (public L2 and recovered L3) |

Dual-feed UDP + WAL are exercised together in stress/recovery harnesses
(`bench/lockstep_fault_stress`, `bench/lockstep_crash_matrix`). Details:
`docs/VERIFICATION.md`, `docs/DURABILITY.md`.
