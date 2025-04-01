# Durability

## Overview

Lockstep provides durable persistence for crash recovery. This document describes the durability model, guarantees, and limitations.

## Write-Ahead Log (WAL)

The WAL records every command in order:

- **Magic**: 0x57414C4B ("WALK")
- **Version**: 1
- **Record format**:
  - 4 bytes: magic
  - 1 byte: version
  - 1 byte: record kind
  - 2 bytes: payload length
  - 8 bytes: command sequence
  - 8 bytes: timestamp
  - N bytes: payload
  - 4 bytes: CRC32C

### Write Ordering

For strict durable mode:

1. Parse and validate frame
2. Append to WAL
3. Flush and fsync
4. Apply to engine state
5. Acknowledge client

This ensures acknowledged commands are recoverable.

### Recovery

On startup:

1. Find newest valid snapshot
2. Load snapshot state
3. Replay WAL from snapshot point
4. Ignore incomplete tail record
5. Fail on mid-record corruption

## Snapshots

Snapshots capture complete engine state:

- Instrument configuration
- All live orders in priority order
- Client risk state
- Command and event sequences
- Client sequence high-water marks

### Snapshot Format

```
Header:
  4 bytes: magic (0x534E4150 = "SNAP")
  1 byte: version
  3 bytes: reserved
  8 bytes: timestamp
  8 bytes: command_seq
  8 bytes: event_seq
  ... additional fields

Body:
  Instruments
  Orders
  Risk state

Footer:
  4 bytes: CRC32C
```

### Atomic Write

Snapshots are written atomically:

1. Write to temporary file
2. Flush and fsync
3. Rename to final name
4. Sync parent directory

## Tested Scenarios

| Scenario | Result |
|----------|--------|
| Clean shutdown | Full recovery |
| Kill during WAL write | Last complete record recovered |
| Kill after fsync | Full recovery |
| Kill during snapshot | Previous snapshot used |
| WAL corruption at tail | Tail record ignored |
| WAL corruption mid-file | Recovery fails |

## Limitations

**Not protected against:**

- Sudden power loss
- Disk firmware behavior
- Controller cache
- Filesystem corruption
- Operating system crashes

**Assumptions:**

- POSIX filesystem semantics
- fsync correctly flushes to disk
- Atomic rename works correctly

## Testing

Durability is tested by:

1. Simulated process kills
2. WAL truncation at various points
3. Snapshot interruption
4. Checksum validation

See `tests/recovery/test_recovery.cpp` for test details.
