# Known Errors & Potential Issues

Errors are documented here when spotted. They are NOT fixed unless explicitly requested.
Status: `open` | `fixed`

---

## ERR-001 — `add_market_order` has no return statement (UB)

**Status:** fixed  
**Severity:** High  
**File:** `src/order_book.cpp:114–129`

Function is now implemented with matching logic and a return statement. Resolved.

---

## ERR-002 — Stale `order_look_up_` entries after a fill (dangling iterator)

**Status:** fixed  
**Severity:** Medium  
**Files:** `src/order_book.cpp` (`execute_trade`)

`execute_trade` now calls `remove_look_up(front_order_id)` when the resting front order is confirmed fully filled, before `fill_front_order` erases it from the list. Regression covered by test `tc_cancel_after_fill`.
