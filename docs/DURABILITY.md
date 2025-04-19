# Durability

## Overview

Lockstep persists commands and recovers exchange state after process death.
Combined with **dual UDP market-data feeds**, the recovery story is validated
under fault injection with resume gates of **100M** stressed logical events and
**10K** recovery trials at **zero** state digest mismatches.

## Write-ahead log (WAL)

Every command is recorded in order:

- **Magic**: 0x57414C4B (`WALK`)
- **Version**: 1
- **Record**: magic, version, kind, payload length, command sequence, timestamp,
  payload, CRC32C

### Write ordering (strict durable mode)

1. Parse and validate frame
2. Append to WAL
3. Flush and fsync
4. Apply to engine state
5. Acknowledge client

Acknowledged commands are recoverable from durable media (subject to the
hardware limitations listed below).

### Recovery

1. Find newest valid snapshot
2. Load snapshot state
3. Replay WAL from the snapshot point
4. Ignore incomplete tail record
5. Fail closed on mid-file corruption

## Snapshots

Snapshots capture instrument config, live orders in priority order, risk state,
command/event sequences, and client sequence high-water marks.

Atomic write: temp file → fsync → rename → sync parent directory.

## Dual-feed UDP and WAL together

Production-shaped path:

```
UDP Feed A ─┐
            ├─► fault proxy (tests) ─► arbiter ─► digest / subscriber state
UDP Feed B ─┘
Commands ─► WAL ─► matching engine ─► snapshots
```

- Dual feeds heal single-channel loss and detect gaps via sequences
- WAL + snapshots rebuild engine state after kills and truncated writes
- Stress harness injects loss / duplicate / reorder / corruption / outages
- Crash matrix walks prefix truncations, mid-snapshot kills, and replay points

### Resume gates

| Metric | Gate |
| --- | --- |
| Logical events under faults | **100M** |
| Distinct recovery scenarios | **10K** |
| L2 / L3 digest mismatches | **0** |

See `docs/VERIFICATION.md` for counting rules and harness names.

## Tested scenarios

| Scenario | Result |
|----------|--------|
| Clean shutdown | Full recovery |
| Kill during WAL write | Last complete record recovered |
| Kill after fsync | Full recovery |
| Kill during snapshot | Previous snapshot used |
| WAL corruption at tail | Tail ignored |
| WAL corruption mid-file | Recovery fails closed |
| Single UDP channel outage | Heal via alternate feed |
| Fault-injected dual-feed stream | Zero digest mismatches at gate scale |

## Limitations

Not protected against: sudden power loss past controller cache, disk firmware
lies, filesystem corruption, kernel panics.

Assumptions: POSIX semantics, `fsync` reaches stable media, atomic rename works.

## Testing

```bash
make test      # includes recovery unit/integration
make stress    # 100M fault events + 10K recovery aggregate
```

Primary sources: `tests/recovery/`, `bench/lockstep_fault_stress.cpp`,
`bench/lockstep_crash_matrix.cpp`.
