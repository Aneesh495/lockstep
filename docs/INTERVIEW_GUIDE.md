# Interview Guide

## 30-Second Summary

Lockstep is a C++20 exchange matching engine with deterministic semantics, dual-feed UDP market data, WAL-based persistence, and comprehensive testing. Built from scratch to demonstrate low-latency systems expertise.

## 2-Minute Explanation

Lockstep implements a complete electronic trading engine:

1. **Matching Engine**: Price-time priority, supports GTC/IOC/FOK, fixed-capacity data structures, zero heap allocations after init.

2. **Risk Controls**: Per-client limits, position tracking, kill switch.

3. **Networking**: TCP order entry, dual redundant UDP market data feeds with gap detection and recovery.

4. **Persistence**: Write-ahead log with CRC32C, snapshot recovery, process-termination resilience.

5. **Testing**: Differential testing against reference implementation, fault injection, sanitizers, fuzzing.

The design prioritizes correctness first, then performance. Every component is tested with invariants. The benchmark demonstrates the zero-allocation hot path.

## 10-Minute Deep Dive

### Design Decisions

1. **Why price-time priority?**
   - Standard exchange model
   - Predictable behavior for traders
   - FIFO fairness within price level

2. **Why no external dependencies?**
   - Control over behavior
   - No hidden allocations
   - Reproducible builds

3. **Why dual UDP feeds?**
   - Standard practice for market data
   - Gap detection without full retransmission
   - Recovery from single-channel loss

4. **Why object pool instead of smart pointers?**
   - Zero heap allocation guarantee
   - Cache locality
   - Deterministic capacity

5. **Why separate command and event sequences?**
   - Commands are inputs
   - Events are outputs
   - Different ordering requirements

### Trade-offs

| Choice | Benefit | Cost |
|--------|---------|------|
| Single-threaded engine | Determinism | Scalability limit |
| Fixed capacity | No allocation | Memory overhead |
| CRC32C | Fast verification | Not cryptographic |
| TCP order entry | Reliability | Latency overhead |

### Limitations

- No live trading integration
- No FPGA acceleration
- No kernel bypass
- Single matching thread
- Local network only

## Questions

1. **What's the matching algorithm?**
   Best price first, FIFO within level. See `docs/MATCHING_RULES.md`.

2. **How do you handle self-trades?**
   Cancel aggressor, preserve passive order.

3. **What's the allocation strategy?**
   Preallocate everything. Track with allocation counter.

4. **How does recovery work?**
   Load snapshot, replay WAL. Skip incomplete tail records.

5. **How do you test correctness?**
   Differential testing, invariants, sanitizers, fuzzing.

6. **What's the threading model?**
   Single engine thread, gateway on separate thread, SPSC queues.

7. **How do you handle backpressure?**
   Bounded SPSC rings, reject when full.

8. **What happens on process kill?**
   Recover from last consistent snapshot/WAL point.

9. **How do you measure latency?**
   Per-operation timestamps, nearest-rank percentiles.

10. **What optimizations were applied?**
    Preallocation, cache-friendly data structures, bitset best-price tracking.

11. **What would you do differently?**
    Consider mmap for WAL, investigate io_uring, add multithreaded matching.

12. **How would you scale this?**
    Partition by instrument, add gateway replicas, consider shared-nothing architecture.

13. **What's the performance?**
    See `docs/BENCHMARKS.md` for measured results.

14. **How do you handle clock sync?**
    Virtual clock for determinism, wall clock only for network timestamps.

15. **What's the FOK algorithm?**
    Preflight quantity check, then all-or-nothing execution.

16. **How does risk tracking work?**
    Reserve exposure on new order, release on fill/cancel.

17. **What's the protocol format?**
    40-byte frame header, fixed-width payloads. See `docs/PROTOCOL.md`.

18. **How do you handle malformed input?**
    Reject at parse time, no undefined behavior.

19. **What's tested by fuzzing?**
    Frame decoding, WAL records, snapshots.

20. **How does the reference book work?**
    std::map + std::deque, simple implementation for comparison.

21. **What's the hash table strategy?**
    Robin Hood with backward-shift deletion.

22. **How do you ensure determinism?**
    Same seed + config + binary = same output.

23. **What about GCC vs Clang?**
    Both tested in CI, same semantics required.

24. **How does feed arbitration work?**
    Merge A and B, dedupe by event sequence, gap detection.

25. **What's the snapshot format?**
    Canonical binary with CRC32C, see `docs/DURABILITY.md`.
