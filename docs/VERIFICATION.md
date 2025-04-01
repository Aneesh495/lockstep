# Verification

## Test Categories

### Unit Tests

Unit tests verify individual components in isolation:

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

### Property Tests

Property tests verify invariants across random inputs:

| File | Properties |
|------|------------|
| test_differential.cpp | Optimized vs reference book |

### Integration Tests

Integration tests verify component interactions:

| File | Coverage |
|------|----------|
| test_network.cpp | TCP/UDP operations |
| test_recovery.cpp | Crash recovery |

### Golden Tests

Golden tests verify against known outputs:

| File | Coverage |
|------|----------|
| test_golden.cpp | CRC32C and SHA256 check vectors |

## Invariant Checking

The invariant checker verifies:

1. Book is not crossed
2. Order counts match pool size
3. Price level quantities match sum of orders
4. Order indices are consistent
5. Free list is correct
6. Sequences are monotonic

Run with `checkInvariants()` after each operation in tests.

## Sanitizers

### AddressSanitizer (ASan)

Detects memory errors:
- Buffer overflows
- Use-after-free
- Memory leaks

Run: `make sanitize`

### ThreadSanitizer (TSan)

Detects data races:
- Concurrent access
- Missing synchronization

Run: `make tsan`

### UndefinedBehaviorSanitizer (UBSan)

Detects undefined behavior:
- Integer overflow
- Null pointer dereference
- Misaligned access

Run with sanitize target (includes UBSan).

## Fuzzing

Fuzz targets exercise parsing with random input:

| Target | Coverage |
|--------|----------|
| fuzz_frame_decoder.cpp | Frame parsing |
| fuzz_wal_decoder.cpp | WAL record parsing |
| fuzz_snapshot_decoder.cpp | Snapshot parsing |

Run: `make fuzz-smoke`

## Coverage

Coverage is measured for:

- Line coverage
- Branch coverage
- Function coverage

Target: 80%+ line coverage on core components.

## Commands

```bash
# Run all tests
make test

# Run with sanitizers
make sanitize

# Run ThreadSanitizer
make tsan

# Run fuzz smoke tests
make fuzz-smoke

# Verify build and tests
make verify
```
