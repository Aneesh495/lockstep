# Matching Rules

## Order Types

### Limit Orders

All orders are limit orders. There are no market orders - marketable flow uses marketable limit orders.

### Time in Force

| TIF | Behavior |
|-----|----------|
| GTC | Good Till Cancel - rests until filled or canceled |
| IOC | Immediate or Cancel - any unfilled portion is canceled |
| FOK | Fill or Kill - must fill completely or reject |

## Priority

1. **Price priority**: Best price trades first
   - For bids: highest price first
   - For asks: lowest price first

2. **Time priority**: Within a price level, FIFO order
   - Earlier orders have priority
   - Replace that changes price loses priority
   - Replace that increases quantity loses priority
   - Replace that only decreases quantity keeps priority

## Matching Algorithm

### Step 1: Validate

- Check instrument exists
- Check price within band
- Check quantity > 0
- Check no duplicate order ID

### Step 2: Check Risk

- Verify quantity limits
- Verify notional limits
- Verify open order limits
- Verify position limits
- Check kill switch

### Step 3: Match (for marketable orders)

For a marketable buy:
1. Find best ask (lowest price)
2. If limit price >= best ask price, match
3. Continue until order filled or no matching asks

For a marketable sell:
1. Find best bid (highest price)
2. If limit price <= best bid price, match
3. Continue until order filled or no matching bids

### Step 4: Rest (for GTC with remainder)

- Add to appropriate price level
- Assign timestamp
- Add to order index

## Self-Trade Prevention

When an aggressive order would match against a resting order from the same client:

1. Cancel the aggressive remainder
2. Do not execute the trade
3. Return rejection

Policy: Cancel aggressor, preserve passive order.

## FOK Handling

For Fill-or-Kill orders:

1. Calculate total available quantity at limit price or better
2. Check risk for full quantity
3. If fillable and passes risk, execute
4. If not fillable or fails risk, reject without mutation

FOK never partially fills. It either fills completely or rejects.

## Replace Semantics

A replace operation:

1. Creates a new order ID
2. Retires the old order ID
3. If price changes: loses priority
4. If quantity increases: loses priority
5. If quantity decreases only: keeps priority

Replace is atomic - the old order is removed and new order added in one operation.

## Edge Cases

### Crossing the Spread

When a new order would cross the spread:
- It matches aggressively first
- Any remainder rests as a limit order

### Empty Book

When the book is empty:
- Any new order rests immediately
- No matching occurs

### IOC with No Liquidity

When an IOC order finds no matching liquidity:
- It is immediately canceled
- Returns with filled_quantity = 0

### Duplicate Order ID

When a new order has a duplicate order ID:
- Reject with DuplicateOrderId
- No state change

### Cancel Non-Existent Order

When canceling an order that doesn't exist:
- Reject with OrderNotFound
- No state change
