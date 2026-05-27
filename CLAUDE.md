# CLAUDE.md — Project Context

This file is loaded automatically by Claude Code. Keep it up to date as the project grows.

---

## Project Vision

A from-scratch, low-latency quantitative trading system built in C++ (primary), with possible additions in other languages for specific components (e.g. Python for backtesting/analysis, maybe Rust for ultra-low-latency paths). The goal is to implement the core infrastructure of a quant firm's trading stack, component by component.

**Planned components (not all started):**

| Component | Status | Notes |
|---|---|---|
| Local order book | In progress | Core matching engine done |
| Market data feed handler | Planned | Receives and normalises raw feed data |
| Data parser | Planned | Decode binary/FIX protocols |
| Algorithmic trading engine | Planned | Signal generation, order routing |
| Backtesting framework | Planned | Replay historical data against strategies |
| Market replay | Planned | Reconstruct market state from historical feed |
| Latency logging | Planned | Timestamping at key pipeline stages |
| Performance measurement | Planned | Benchmarks, throughput, tick-to-trade latency |
| Frontend / dashboard | Possible | May be added later; language TBD |

---

## Current Status

The **order book** component is the first piece being built. It implements:
- Price-time priority (FIFO) matching
- GTC, IOC, and FOK time-in-force
- Bid/ask level management with a price-level abstraction
- Order lookup table for O(1) cancel
- Trade history

A **data-driven test framework** sits alongside the order book with 17 named test cases covering all TIF types, FIFO ordering, multi-level sweeps, and cancel behaviour.

---

## Project Structure

```
MyOrderBook/
├── include/            # Public headers
│   ├── types.h         # Aliases: Price, Quantity, OrderId, Side, TimeInForce
│   ├── helpers.h       # to_string() for enums, ostream operators
│   ├── order.h         # Order class
│   ├── price_level.h   # PriceLevel — one side of one price
│   ├── order_book.h    # OrderBook — full matching engine
│   └── trade.h         # Trade + TradeInfo
├── src/                # Implementations
│   ├── order.cpp
│   ├── price_level.cpp
│   ├── order_book.cpp
│   ├── trade.cpp
│   └── helpers.cpp
├── test/               # Test framework and cases
│   ├── data_driven.h   # Structs, macros, function declarations
│   └── data_driven.cpp # check_trades, check_levels, 17 named test cases
├── doc/                # Project documentation
│   ├── rule.md         # Collaboration rules for Claude Code
│   └── error.md        # Spotted bugs and open issues (ERR-NNN)
├── test.cpp            # Test entry point
├── main.cpp            # Manual scratchpad / integration testing
├── run.sh              # Quick release build (runs main.cpp)
├── run_debug.sh        # Debug build with ASan+UBSan (runs test.cpp)
└── CLAUDE.md           # This file
```

---

## Build Commands

### Run tests (debug + sanitizers)
```bash
bash run_debug.sh
# expands to:
clang++ -std=c++20 -Iinclude -Itest -Wall -Wextra -Wpedantic \
        -fsanitize=address,undefined -g \
        src/*.cpp test/data_driven.cpp test.cpp -o build/test
./build/test
```

### Run main scratchpad
```bash
bash run.sh
# expands to:
g++ -std=c++20 -Iinclude src/*.cpp main.cpp -o main && ./main
```

> **Note:** `run_debug.sh` must include `test/data_driven.cpp` explicitly — it is not under `src/`.

---

## Key Design Decisions

- **Price levels** are `std::map` (asks ascending, bids descending via `std::greater`). Front iterator is always the best price.
- **FIFO** within a level is maintained by `std::list<Order>` inside `PriceLevel`. Incoming orders are pushed to the back; matching starts from the front.
- **Order lookup** is an `std::unordered_map<OrderId, OrderLocation>` for O(1) cancel by ID.
- **TIF handling:** FOK checks available liquidity before any matching via `can_fully_fill_against_side`. IOC and GTC share the same matching path; IOC simply never rests if unfilled.
- **Execution price** is always the resting order's price (maker pricing).

---

## Collaboration Rules

See `doc/rule.md` for the full list. Summary:

1. **Do not modify `src/` or `include/` files** without explicit instruction.
2. **Document spotted bugs** in `doc/error.md` (ERR-NNN format) and notify the user — do not silently fix them.

---

## Open Errors

See `doc/error.md` for full details.

| ID | Severity | Summary |
|---|---|---|
| ERR-001 | High | `add_market_order` has no return statement — UB when called — **fixed** |
| ERR-002 | Medium | Filled orders remain in `order_look_up_` — dangling iterator if cancel is attempted |
