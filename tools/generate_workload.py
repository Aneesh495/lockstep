#!/usr/bin/env python3
"""
Generate deterministic workload for benchmarks.
"""

import argparse
import hashlib
import json
import random
from dataclasses import dataclass
from typing import List, Tuple


@dataclass
class Order:
    order_id: int
    side: str  # 'B' or 'S'
    price: int
    quantity: int
    tif: str  # 'GTC', 'IOC', 'FOK'


@dataclass
class Command:
    cmd_type: str  # 'new', 'cancel', 'replace'
    order: Order
    old_order_id: int = 0
    new_price: int = 0
    new_quantity: int = 0


def generate_workload(
    seed: int,
    num_commands: int,
    new_prob: float = 0.40,
    cancel_prob: float = 0.20,
    replace_prob: float = 0.15,
    marketable_prob: float = 0.15,
    ioc_prob: float = 0.05,
    fok_prob: float = 0.05,
) -> Tuple[List[Command], dict]:
    """Generate deterministic workload with given probabilities."""
    
    random.seed(seed)
    
    commands = []
    active_orders: List[int] = []
    next_order_id = 1
    
    counts = {
        'new_resting_gtc': 0,
        'cancel': 0,
        'replace': 0,
        'marketable': 0,
        'ioc': 0,
        'fok': 0,
    }
    
    for _ in range(num_commands):
        r = random.random()
        
        if r < new_prob or not active_orders:
            # New resting GTC
            order = Order(
                order_id=next_order_id,
                side=random.choice(['B', 'S']),
                price=100 + random.randint(0, 99),
                quantity=10 + random.randint(0, 99),
                tif='GTC',
            )
            next_order_id += 1
            active_orders.append(order.order_id)
            commands.append(Command('new', order))
            counts['new_resting_gtc'] += 1
            
        elif r < new_prob + cancel_prob:
            # Cancel
            idx = random.randint(0, len(active_orders) - 1)
            order_id = active_orders.pop(idx)
            order = Order(order_id=order_id, side='B', price=0, quantity=0, tif='GTC')
            commands.append(Command('cancel', order))
            counts['cancel'] += 1
            
        elif r < new_prob + cancel_prob + replace_prob:
            # Replace
            idx = random.randint(0, len(active_orders) - 1)
            old_order_id = active_orders[idx]
            new_order_id = next_order_id
            next_order_id += 1
            active_orders[idx] = new_order_id
            
            order = Order(
                order_id=new_order_id,
                side='B',
                price=0,
                quantity=0,
                tif='GTC',
            )
            commands.append(Command(
                'replace',
                order,
                old_order_id=old_order_id,
                new_price=100 + random.randint(0, 99),
                new_quantity=10 + random.randint(0, 99),
            ))
            counts['replace'] += 1
            
        elif r < new_prob + cancel_prob + replace_prob + marketable_prob:
            # Marketable limit
            side = random.choice(['B', 'S'])
            price = 200 if side == 'B' else 100
            order = Order(
                order_id=next_order_id,
                side=side,
                price=price,
                quantity=10 + random.randint(0, 99),
                tif='GTC',
            )
            next_order_id += 1
            commands.append(Command('new', order))
            counts['marketable'] += 1
            
        elif r < new_prob + cancel_prob + replace_prob + marketable_prob + ioc_prob:
            # IOC
            order = Order(
                order_id=next_order_id,
                side=random.choice(['B', 'S']),
                price=150,
                quantity=10 + random.randint(0, 99),
                tif='IOC',
            )
            next_order_id += 1
            commands.append(Command('new', order))
            counts['ioc'] += 1
            
        else:
            # FOK
            order = Order(
                order_id=next_order_id,
                side=random.choice(['B', 'S']),
                price=150,
                quantity=10 + random.randint(0, 99),
                tif='FOK',
            )
            next_order_id += 1
            commands.append(Command('new', order))
            counts['fok'] += 1
    
    return commands, counts


def main():
    parser = argparse.ArgumentParser(description='Generate benchmark workload')
    parser.add_argument('--seed', type=int, default=12345, help='Random seed')
    parser.add_argument('--count', type=int, default=10000000, help='Number of commands')
    parser.add_argument('--output', type=str, default='-', help='Output file')
    
    args = parser.parse_args()
    
    commands, counts = generate_workload(args.seed, args.count)
    
    # Compute hash
    h = hashlib.sha256()
    h.update(str(args.seed).encode())
    h.update(str(args.count).encode())
    workload_hash = h.hexdigest()[:16]
    
    result = {
        'seed': args.seed,
        'count': args.count,
        'hash': workload_hash,
        'counts': counts,
    }
    
    if args.output == '-':
        print(json.dumps(result, indent=2))
    else:
        with open(args.output, 'w') as f:
            json.dump(result, f, indent=2)


if __name__ == '__main__':
    main()
