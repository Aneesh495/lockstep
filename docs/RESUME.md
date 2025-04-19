# Resume Bullets

Mapped to documentation and evidence. Wording matches the public README gates.

## Performance

Built a zero-allocation price-time matching core, benchmarking **5M+ commands/s**
and **&lt;1 µs p99** in isolated tests.

| Claim | Where it is documented | Evidence |
| --- | --- | --- |
| Zero-allocation hot path | README, ARCHITECTURE, BENCHMARKS | `AllocationCounter`; bench acceptance |
| Price-time matching | README, MATCHING_RULES, ARCHITECTURE | engine + differential tests |
| ≥5M commands/s (isolated) | BENCHMARKS, ARCHITECTURE | `results/verified/` benchmark summaries |
| p99 &lt;1 µs (isolated, separate harness) | BENCHMARKS, ARCHITECTURE | latency histogram / bench JSON |

## Resilience

Engineered **dual-feed UDP / WAL** recovery with **zero mismatches** across
**100M** fault-injected events and **10K** recoveries.

| Claim | Where it is documented | Evidence |
| --- | --- | --- |
| Dual UDP feeds + WAL | README, ARCHITECTURE, DURABILITY | publishers, arbiter, WAL/snapshot |
| 100M fault-injected logical events | VERIFICATION, DURABILITY | stress shards / STRESS summaries |
| 10K recovery trials | VERIFICATION, DURABILITY | crash matrix / RECOVERY summaries |
| Zero state mismatches | VERIFICATION, DURABILITY | digest mismatch counters == 0 |

---

Gates are conservative floors. Observed Apple Silicon medians may exceed 5M
commands/s; resume language stays on the documented thresholds.
