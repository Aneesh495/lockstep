# Architecture

## Overview

Lockstep is a low-latency deterministic electronic exchange engine built in C++20. It implements a price-time priority matching engine with risk controls, network interfaces, and durable persistence.

The design prioritizes correctness and determinism over raw throughput. Given the same sequence of inputs, the engine produces identical outputs. This is a hard requirement for regulatory compliance, audit trails, and debugging production incidents in real trading systems.

## Components

### Matching Engine

The core matching engine (`engine/matching_engine.hpp`) processes orders in a single-threaded deterministic manner:

- Single writer model - all book mutations happen on one thread
- Price-time priority - FIFO within price level, best price first
- Fixed-capacity data structures - no heap allocations after init
- Reference book for differential testing

The matching algorithm is standard: when a new order arrives, check if it crosses the opposite side of the book. If so, match at the best price, working through levels until either the order is filled or no more matches are possible.

### Order Book

The optimized order book (`engine/order_book.hpp`) uses:

- Preallocated price level array indexed by price offset
- Bitset-based best bid/ask tracking
- Object pool for order storage with intrusive free list
- Robin Hood hash table for order lookup

The book is organized as two arrays of price levels, one for bids and one for asks. Each price level contains a linked list of orders in arrival order. The best bid/ask are tracked using a bitset for O(1) best price updates.

### Risk Engine

The risk engine (`risk/risk_engine.hpp`) enforces:

- Per-client position and order limits
- Reservation-based exposure tracking
- Kill switch capability

Risk checks run before orders reach the matching engine. This keeps latency minimal: the risk engine is stateless for orders, tracking only aggregate positions and order counts.

### Persistence

The persistence layer provides:

- Write-ahead log with CRC32C verification
- Snapshot-based recovery
- Atomic file operations

Every incoming command is serialized and appended to a file before execution. The WAL uses the "WALK" magic number and CRC32C checksums for integrity.

### Networking

The network layer implements:

- TCP gateway for order entry
- Dual UDP publishers for market data
- Feed arbiter for redundant stream handling

The TCP gateway accepts client connections, decodes framed messages, forwards to matching engine. UDP publishers broadcast market data on two independent feeds (A and B). Subscribers can detect gaps via sequence numbers and request retransmission from the alternate feed.

## Threading Model

```
                    ┌─────────────────┐
                    │ TCP Gateway     │
                    │ (Parser Thread) │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ SPSC Ring       │
                    │ (Commands)      │
                    └────────┬────────┘
                             │
                             ▼
┌──────────────┐    ┌─────────────────┐    ┌──────────────┐
│ UDP Pub A    │◄───│ Matching Engine │───►│ UDP Pub B    │
└──────────────┘    │ (Engine Thread) │    └──────────────┘
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ SPSC Ring       │
                    │ (Responses)     │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ TCP Gateway     │
                    │ (Sender Thread) │
                    └─────────────────┘
```

The single-threaded matching engine eliminates synchronization overhead and ensures deterministic execution. Real exchanges shard by instrument to scale across cores, but each shard remains single-threaded.

## Memory Management

All hot-path structures are preallocated:

- Price levels: vector with size = (max_price - min_price) / tick_size
- Order pool: object pool with fixed capacity
- Order index: Robin Hood hash with fixed capacity
- SPSC rings: power-of-two capacity, preallocated

No heap allocations occur after initialization. The `AllocationCounter` class can verify this at runtime in debug builds.

## Determinism

The engine is deterministic when replaying from WAL:

- Same command sequence produces same state
- Virtual clock for timestamps
- Stable hashing for digests
- No reading of wall-clock time

Non-determinism only appears in:

- Network packet arrival order (gateway assigns sequence)
- Session IDs (generated on startup)

This is verified by the `test_differential` property test, which runs random operations against both the optimized `OrderBook` and the reference `ReferenceBook`, then checks that both produce identical results.

## Failure Model

The system handles:

- Process termination: Recover from snapshot + WAL
- Network faults: Dual UDP feed with gap detection
- Disk write failures: CRC validation, fsync

Not handled:

- Sudden power loss (depends on hardware write caching)
- Disk corruption
- Kernel panics

## Performance Characteristics

On Apple Silicon M3:

| Operation | Latency (p50) | Latency (p99) |
|-----------|---------------|---------------|
| New order (no match) | ~50ns | ~200ns |
| New order (with match) | ~100ns | ~500ns |
| Cancel order | ~30ns | ~100ns |
| Replace order | ~80ns | ~300ns |

Throughput: ~29M operations/second (single-threaded, release build)

Memory: ~128 bytes per active order, plus fixed overhead for price levels
