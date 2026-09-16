# Protocol Specification

## Frame Format

All messages use a 40-byte frame header:

```
Offset  Size  Field
0       4     magic (0x4C4B5354 = "LKST")
4       1     version (1)
5       1     message_type
6       2     flags
8       4     payload_length
12      4     session_id
16      8     sequence
24      8     send_timestamp_ns
32      4     crc32c
36      4     reserved (must be zero)
```

All integers are in network byte order (big-endian).

CRC32C covers the header with crc32c field set to zero, followed by the payload.

## Message Types

### Client -> Exchange

| Type | Code | Description |
|------|------|-------------|
| NewOrder | 1 | Submit new limit order |
| CancelOrder | 2 | Cancel existing order |
| ReplaceOrder | 3 | Modify existing order |
| MassCancel | 4 | Cancel all orders for client |
| SnapshotRequest | 5 | Request book snapshot |
| Heartbeat | 6 | Connection keepalive |

### Exchange -> Client

| Type | Code | Description |
|------|------|-------------|
| OrderAccepted | 101 | Order acknowledged |
| OrderRejected | 102 | Order rejected |
| OrderCanceled | 103 | Order canceled |
| OrderReplaced | 104 | Order replaced |
| OrderExecuted | 105 | Trade execution |
| MassCancelDone | 106 | Mass cancel complete |
| SnapshotBegin | 107 | Snapshot start |
| SnapshotRow | 108 | Snapshot data row |
| SnapshotEnd | 109 | Snapshot complete |
| HeartbeatAck | 110 | Heartbeat response |

### Market Data

| Type | Code | Description |
|------|------|-------------|
| BookAdd | 201 | Price level added |
| BookChange | 202 | Price level changed |
| BookDelete | 203 | Price level removed |
| Trade | 204 | Trade execution |
| TradingStatus | 205 | Instrument status change |

## Payload Formats

### NewOrder (56 bytes)

```
Offset  Size  Field
0       4     client_id
4       8     order_id
12      4     instrument_id
16      1     side (0=Buy, 1=Sell)
17      1     time_in_force (0=GTC, 1=IOC, 2=FOK)
18      2     padding
20      8     price (signed ticks)
28      4     quantity
32      8     client_sequence
40      8     client_timestamp
48      8     reserved
```

### CancelOrder (32 bytes)

```
Offset  Size  Field
0       4     client_id
4       8     order_id
12      8     client_sequence
20      8     client_timestamp
28      4     reserved
```

### OrderAccepted (56 bytes)

```
Offset  Size  Field
0       4     client_id
4       8     order_id
12      4     instrument_id
16      1     side
17      1     time_in_force
18      2     padding
20      8     price
28      4     quantity
32      8     engine_sequence
40      8     engine_timestamp
48      8     reserved
```

### OrderRejected (32 bytes)

```
Offset  Size  Field
0       4     client_id
4       8     order_id
12      2     rejection_reason
14      2     padding
16      8     engine_sequence
24      8     engine_timestamp
```

## Rejection Reasons

| Code | Reason |
|------|--------|
| 0 | None |
| 1 | UnknownInstrument |
| 2 | InvalidPrice |
| 3 | InvalidQuantity |
| 4 | InvalidSide |
| 5 | InvalidTimeInForce |
| 6 | DuplicateOrderId |
| 7 | OrderNotFound |
| 8 | OrderNotLive |
| 9 | InsufficientQuantity |
| 10 | MaxOrderQuantityExceeded |
| 11 | MaxOrderNotionalExceeded |
| 12 | MaxOpenOrdersExceeded |
| 13 | MaxOpenQuantityExceeded |
| 14 | MaxOpenNotionalExceeded |
| 15 | MaxPositionExceeded |
| 16 | KillSwitchActive |
| 17 | PriceBandViolation |
| 18 | SelfTradePrevention |
| 19 | FOKCannotFill |
| 20 | SequenceTooOld |
| 21 | InstrumentSuspended |
| 22 | CapacityExceeded |
| 23 | InvalidMessage |
| 24 | InternalError |

## Example: New Order

```
Header:
  magic:            0x4C4B5354
  version:          1
  message_type:     1 (NewOrder)
  flags:            0
  payload_length:   56
  session_id:       1
  sequence:         100
  send_timestamp:   1234567890000000000

Payload:
  client_id:        1
  order_id:         100
  instrument_id:    1
  side:             0 (Buy)
  tif:              0 (GTC)
  price:            10000
  quantity:         100
```
