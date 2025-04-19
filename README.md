# Lockstep

A low-latency **deterministic exchange matching and recovery engine** in C++20:
price-time priority matching, pre-trade risk, dual UDP market-data feeds, and
WAL + snapshot crash recovery - built so an engineer can verify the hot path
and the fault story from the docs and harnesses alone.

## Design in one page

- **Single-threaded matching core** - one writer, deterministic book mutations
- **Zero-allocation hot path** after init (fixed pools, fixed price levels)
- **Isolated core benches** - throughput and latency measured separately (no
  network, no WAL) against explicit resume gates
- **Dual-feed UDP + WAL recovery** - redundant market-data channels and durable
  command log; stress and recovery matrices demand **zero state mismatches**

```
                 +------------------+
            +--->| UDP Feed A :5001 |
            |    +------------------+
 Clients    |    +--------+
 (TCP:8080)-+--->| Match  |---- WAL / snapshots
            |    | Engine |
            |    +--------+
            +--->| UDP Feed B :5002 |
                 +------------------+
                      |
                      v
              fault proxy / arbiter (tests)
                      |
                      v
              state digest == 0 mismatches
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the threading model and
component map.

## Documented performance and resilience gates

These are the numbers the README and benches are written to support. Methodology
and isolation rules: [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md),
[`docs/VERIFICATION.md`](docs/VERIFICATION.md),
[`docs/DURABILITY.md`](docs/DURABILITY.md).

| Gate | Claim | Isolation |
| --- | --- | --- |
| Matching throughput | **≥5M commands/s** | In-memory core only (no TCP/UDP, no WAL I/O) |
| Matching latency | **p99 &lt;1 µs** | Separate one-command-at-a-time latency harness |
| Allocations | **Zero** heap allocs on the hot path after init | `AllocationCounter` |
| Fault stress | **100M** logical events under fault injection | Dual-feed path + arbiter/digest logic |
| Recovery matrix | **10K** recovery trials | Snapshot + WAL prefix / kill / truncate cases |
| Correctness | **Zero** public L2 / L3 state digest mismatches | Across stress + recovery aggregates |

Observed Apple Silicon Release medians on the same isolated core often land
well above the throughput gate (tens of millions of ops/s). The **resume gate
remains 5M+ / &lt;1 µs p99** so claims stay conservative and comparable.

## Features

- Price-time priority book: GTC, IOC, FOK; self-trade prevention
- Per-client pre-trade risk (position, size, count) + kill switch
- Write-ahead log with CRC32C; atomic snapshots for fast restart
- TCP order-entry gateway; dual redundant UDP market-data feeds with gap detect
- Differential testing vs a reference book; seeded fault proxy
- Fixed-point prices; no IEEE rounding on the match path

## Quick start

```bash
git clone https://github.com/Aneesh495/lockstep.git
cd lockstep
make build
make test
make demo
```

```bash
./build/lockstep_exchange   # TCP 8080; UDP 5001 / 5002
./build/lockstep_client
```

## Order book (price-time)

Better prices first; FIFO within a price. Self-trade prevention rejects an
aggressor that would match the same client's resting liquidity.

```
Book:
  Bids: 100.05 x 500, 100.00 x 1000
  Asks: 100.10 x 300, 100.15 x 800

Incoming: Sell 700 @ 100.05

  Matches 500 @ 100.05; rests 200 @ 100.05 as new best ask
```

## Persistence and dual-feed recovery

Every command is appended to the WAL before apply (strict durable mode also
fsyncs). Snapshots capture live orders, risk, and sequences. On restart: load
newest valid snapshot, replay WAL, ignore incomplete tail, fail on mid-file
corruption.

Market data publishes on **Feed A and Feed B**. Subscribers detect gaps via
sequence numbers and heal from the alternate channel. Fault injection
(loss / duplicate / reorder / corruption / channel outage) drives the
**100M-event** stress path; crash/truncate matrices drive **10K recoveries** -
both gated on **zero digest mismatches**.

## Wire protocol

40-byte framed header (`LKST` magic), CRC32C, 1400-byte payload cap for single
UDP datagrams. Full layout: [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Testing and benches

```bash
make test              # unit + integration + differential
make sanitize          # ASan + UBSan
make tsan              # ThreadSanitizer
make fuzz-smoke        # decoder fuzz targets
make benchmark         # isolated throughput + latency (resume gates)
make stress            # 100M fault events + 10K recovery aggregate
```

## Documentation

| Doc | Contents |
| --- | --- |
| [Architecture](docs/ARCHITECTURE.md) | Components, threads, memory, failure model |
| [Benchmarks](docs/BENCHMARKS.md) | Isolated 5M+/s and &lt;1 µs p99 methodology |
| [Verification](docs/VERIFICATION.md) | Tests, sanitizers, 100M / 10K gates |
| [Durability](docs/DURABILITY.md) | WAL, snapshots, recovery semantics |
| [Matching rules](docs/MATCHING_RULES.md) | Price-time, STP, order types |
| [Protocol](docs/PROTOCOL.md) | Wire format |
| [Resume](docs/RESUME.md) | Bullet ↔ evidence map |

## Build

C++20, CMake 3.24+, POSIX (Linux / macOS).

```bash
make release    # or: cmake -DCMAKE_BUILD_TYPE=Release && make -j
```

```
include/lockstep/   engine, risk, persistence, network, protocol, containers
src/                implementations
apps/               exchange, client, demo
tests/              unit / property / integration / recovery
bench/              throughput, latency, fault stress, crash matrix
fuzz/               frame / WAL / snapshot decoders
docs/               design + verification
```

## Disclaimer

Educational systems code. Not a production venue. Protocol is original and not
compatible with any live exchange. Use at your own risk.
