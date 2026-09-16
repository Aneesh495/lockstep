# Lockstep

A low-latency deterministic exchange matching engine.

Lockstep is a complete implementation of an electronic trading exchange in C++20. It demonstrates the core components that power real trading venues: price-time priority matching, pre-trade risk controls, network protocols, and crash recovery.

## What This Is

This is a functioning exchange engine: the kind of software that sits at the heart of electronic markets. Given the same sequence of orders, it produces identical results every time. That determinism matters for regulatory compliance, debugging production incidents, and ensuring audit trails match reality.

The design trades raw throughput for correctness. Single-threaded matching eliminates synchronization overhead and the subtle non-determinism that creeps into concurrent systems. Fixed memory allocation means no garbage collection pauses or allocator fragmentation. Fixed-point arithmetic avoids the chaos of IEEE 754 rounding.

## Features

- Price-time priority order book with GTC, IOC, and FOK order types
- Self-trade prevention (regulatory requirement in most markets)
- Pre-trade risk limits per client (position, order size, order count)
- Write-ahead log with CRC32C checksums for crash recovery
- Snapshot-based state dumps for fast recovery
- TCP gateway for order entry
- Dual redundant UDP feeds for market data with gap detection
- Differential testing against a reference implementation
- Zero-allocation hot path after initialization

## Quick Start

```bash
git clone https://github.com/Aneesh495/lockstep.git
cd lockstep
make build
make test
make demo
```

The demo runs through a realistic trading scenario: resting orders, matching trades, IOC and FOK handling, risk rejections, and state verification.

## Running the Exchange

Start the matching engine:

```bash
./build/lockstep_exchange
```

Listens on TCP port 8080 for orders. Broadcasts market data on UDP ports 5001 and 5002.

Connect a client:

```bash
./build/lockstep_client
```

## Architecture

```
                        ┌─────────────────┐
                    ┌──►│  UDP Feed A     │
                    │   │  (Port 5001)    │
┌─────────────┐    ┌┴───┐└─────────────────┘
│   Clients   │───►│Match│
│  (TCP/IP)   │    │ Eng │┌─────────────────┐
└─────────────┘    └┬───┘│  UDP Feed B     │
                    │   │  (Port 5002)    │
                    └──►└─────────────────┘
                    │
               ┌────▼───┐
               │  WAL   │
               │ Writer │
               └────────┘
```

Single-threaded matching core. Orders arrive via TCP, get logged to the write-ahead log, execute against the book, and generate market data broadcasts on redundant UDP feeds.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for details.

## The Order Book

Standard price-time priority:

- Better prices execute first
- At the same price, earlier orders execute first
- Self-trade prevention rejects orders that would match against your own resting orders

Example:

```
Book state:
  Bids: 100.05 x 500, 100.00 x 1000
  Asks: 100.10 x 300, 100.15 x 800

Incoming: Sell 700 @ 100.05 (marketable)

Execution:
  - Matches 500 @ 100.05 against best bid
  - Rests 200 @ 100.05 as new best ask

Result:
  Bids: 100.00 x 1000
  Asks: 100.05 x 200, 100.10 x 300, 100.15 x 800
```

## Persistence

Every command is logged to a write-ahead log before execution. Combined with periodic snapshots, the engine recovers from crashes by loading the latest snapshot and replaying the WAL.

```
WAL Record:
┌──────────────┬─────────┬──────────┬─────────────┬──────────┐
│ Magic "WALK" │ Version │ Type     │ Payload     │ CRC32C   │
│ (4 bytes)    │ (1B)    │ (1B)     │ (variable)  │ (4B)     │
└──────────────┴─────────┴──────────┴─────────────┴──────────┘
```

## Wire Protocol

Frames use a 40-byte header:

```
┌─────────────┬──────────┬──────────────┬─────────────┬─────────┐
│ Magic       │ Version  │ Message Type │ Session ID  │ Seq     │
│ "LKST" (4B) │ (1B)     │ (1B)         │ (4B)        │ (8B)    │
├─────────────┼──────────┼──────────────┼─────────────┼─────────┤
│ Timestamp   │ Payload  │ Reserved     │ CRC32C      │ Payload │
│ (8B)        │ Len (4B) │ (4B)         │ (4B)        │ (var)   │
└─────────────┴──────────┴──────────────┴─────────────┴─────────┘
```

1400-byte payload limit ensures frames fit in single UDP packets.

See [docs/PROTOCOL.md](docs/PROTOCOL.md) for message types and field layouts.

## Performance

On Apple Silicon M3:

| Metric | Value |
|--------|-------|
| Throughput | ~29M ops/sec |
| Median latency | < 100ns |
| Memory per order | ~128 bytes |

Run benchmarks:

```bash
make benchmark
```

## Testing

```bash
make test              # Unit and integration tests
make sanitize          # AddressSanitizer + UBSan
make tsan              # ThreadSanitizer
make fuzz-smoke        # Fuzz targets
```

The test suite includes differential testing: running the same operations against both the optimized order book and a reference implementation, verifying identical results.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) - Design decisions and component layout
- [Protocol](docs/PROTOCOL.md) - Wire format specification
- [Matching Rules](docs/MATCHING_RULES.md) - How orders match
- [Durability](docs/DURABILITY.md) - Crash recovery semantics
- [Benchmarks](docs/BENCHMARKS.md) - Performance methodology
- [Verification](docs/VERIFICATION.md) - Testing approach

## Build Requirements

- C++20 compiler (Clang 14+, GCC 11+, Apple Clang 14+)
- CMake 3.24+
- POSIX system (Linux, macOS)

## Building

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

Or use the Makefile wrapper:

```bash
make build      # Debug
make release    # Release
make sanitize   # With sanitizers
```

## Project Structure

```
lockstep/
├── include/lockstep/   # Headers
│   ├── engine/         # Matching engine, order book
│   ├── risk/           # Risk limits
│   ├── persistence/    # WAL, snapshots
│   ├── network/        # TCP/UDP
│   ├── protocol/       # Wire format
│   └── containers/     # Object pool, hash table
├── src/                # Implementations
├── apps/               # Executables
├── tests/              # Test suite
├── bench/              # Benchmarks
├── fuzz/               # Fuzz targets
└── docs/               # Documentation
```

## Disclaimer

Educational code. Not audited for production. The wire protocol is original and not compatible with any real exchange. Use at your own risk.

---

Built to understand how exchanges work. The goal was correctness and clarity over raw performance, though the single-threaded design naturally achieves low latency. Happy to discuss design decisions.
