#include "data_driven.h"
#include <algorithm>

// ── check_trades ─────────────────────────────────────────────────────────────

void check_trades(const TradeHistory&              actual,
                  const std::vector<ExpectedTrade>& expected) {
    DD_CHECK_EQ((int)actual.size(), (int)expected.size());

    const int n = static_cast<int>(std::min(actual.size(), expected.size()));
    for (int i = 0; i < n; ++i) {
        const Trade&         t  = actual[i];
        const ExpectedTrade& e  = expected[i];
        DD_CHECK_EQ(t.get_buyer().get_order_id(),  e.buyer_id);
        DD_CHECK_EQ(t.get_seller().get_order_id(), e.seller_id);
        DD_CHECK_EQ(t.get_execution_price(),        e.price);
        DD_CHECK_EQ(t.get_execution_quantity(),     e.quantity);
    }
}

// ── check_levels ─────────────────────────────────────────────────────────────

void check_levels(const OrderBook&                  book,
                  const std::vector<ExpectedLevel>& expected) {
    // Tally how many bid/ask levels we expect, then verify the totals.
    // Any unexpected resting level is caught here as a count mismatch.
    std::size_t exp_bids = 0, exp_asks = 0;
    for (const auto& lv : expected) {
        if (lv.side == Side::Buy) ++exp_bids;
        else                      ++exp_asks;
    }
    DD_CHECK_EQ(book.bid_level_count(), exp_bids);
    DD_CHECK_EQ(book.ask_level_count(), exp_asks);

    // Verify each expected level exists with the right quantity (and optional count).
    for (const auto& lv : expected) {
        DD_CHECK(book.has_level(lv.side, lv.price));
        DD_CHECK_EQ(book.level_quantity(lv.side, lv.price), lv.total_quantity);
        if (lv.order_count.has_value()) {
            DD_CHECK_EQ(book.level_order_count(lv.side, lv.price), lv.order_count.value());
        }
    }
}

// ── run_order_book_test ───────────────────────────────────────────────────────

void run_order_book_test(const OrderBookTestCase& tc, bool logging = false) {
    dd::current_test = tc.name;
    std::cout << "[DD] " << tc.name << "\n";

    OrderBook book;
    for (const TestInput& input : tc.inputs) {
        std::visit([&book](const auto& action) {
            using T = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<T, AddOrder>) {
                book.add_order(Order(action.order_type, action.tif, action.id,
                                     action.price, action.qty,
                                     action.side, action.seq));
            } else {
                book.cancel_order(action.id);
            }
        }, input);
    }

    check_trades(book.get_trades(), tc.expected_trades);
    check_levels(book, tc.expected_levels);

    if(logging){
        std::cout << book.get_trades() << "\n";
        std::cout << book << "\n";
    }
}

// ── Example test cases ────────────────────────────────────────────────────────

// Aliases for readability inside test case definitions.
static constexpr TimeInForce GTC = TimeInForce::GoodTillCancel;
static constexpr TimeInForce IOC = TimeInForce::ImmediateOrCancel;
static constexpr TimeInForce FOK = TimeInForce::FillOrKill;
static constexpr OrderType Limit  = OrderType::Limit;
static constexpr OrderType Market = OrderType::Market;

// ── 1. Exact match ────────────────────────────────────────────────────────────
// Two orders of equal size crossing at the same price.
// Both are fully consumed; book is empty afterward.
static const OrderBookTestCase tc_exact_match {
    .name = "exact_match",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},
        AddOrder{Limit, GTC, 2, 100, 10, Side::Buy},
    },
    .expected_trades = {
        {.buyer_id=2, .seller_id=1, .price=100, .quantity=10},
    },
    .expected_levels = {},
};

// ── 2. Partial fill ───────────────────────────────────────────────────────────
// The incoming buy is smaller than the resting sell.
// The sell level survives with the residual quantity.
static const OrderBookTestCase tc_partial_fill {
    .name = "partial_fill",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 20, Side::Sell},
        AddOrder{Limit, GTC, 2, 100,  8, Side::Buy},
    },
    .expected_trades = {
        {.buyer_id=2, .seller_id=1, .price=100, .quantity=8},
    },
    .expected_levels = {
        {.side=Side::Sell, .price=100, .total_quantity=12, .order_count=1},
    },
};

// ── 3. FIFO same-price ────────────────────────────────────────────────────────
// Two sell orders at the same price. A buy that sweeps them must fill the
// earlier-arriving sell first, then the second for the remainder.
static const OrderBookTestCase tc_fifo_same_price {
    .name = "fifo_same_price",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},   // arrives first
        AddOrder{Limit, GTC, 2, 100, 15, Side::Sell},   // arrives second
        AddOrder{Limit, GTC, 3, 100, 12, Side::Buy},    // fills 10 from #1, then 2 from #2
    },
    .expected_trades = {
        {.buyer_id=3, .seller_id=1, .price=100, .quantity=10},
        {.buyer_id=3, .seller_id=2, .price=100, .quantity=2},
    },
    .expected_levels = {
        {.side=Side::Sell, .price=100, .total_quantity=13, .order_count=1},
    },
};

// ── 4. Multi-level sweep ──────────────────────────────────────────────────────
// A single buy order sweeps two distinct ask price levels entirely.
static const OrderBookTestCase tc_multi_level_sweep {
    .name = "multi_level_sweep",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},
        AddOrder{Limit, GTC, 2, 110,  5, Side::Sell},
        AddOrder{Limit, GTC, 3, 110, 15, Side::Buy},    // crosses both levels
    },
    .expected_trades = {
        {.buyer_id=3, .seller_id=1, .price=100, .quantity=10},
        {.buyer_id=3, .seller_id=2, .price=110, .quantity=5},
    },
    .expected_levels = {},
};

// ── 5. GTC rests then matches ─────────────────────────────────────────────────
// An order that cannot cross rests until a later opposing order arrives.
static const OrderBookTestCase tc_gtc_rests_then_matches {
    .name = "gtc_rests_then_matches",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},   // rests
        AddOrder{Limit, GTC, 2,  90,  5, Side::Buy},    // 90 < 100: does not cross, rests
        AddOrder{Limit, GTC, 3,  90,  5, Side::Sell},   // crosses bid #2 at 90
    },
    .expected_trades = {
        {.buyer_id=2, .seller_id=3, .price=90, .quantity=5},
    },
    .expected_levels = {
        {.side=Side::Sell, .price=100, .total_quantity=10, .order_count=1},
    },
};

// ── 6. IOC partial fill ───────────────────────────────────────────────────────
// IOC fills whatever is immediately available; the unfilled remainder is
// cancelled and must not appear as a resting bid.
static const OrderBookTestCase tc_ioc_partial_fill {
    .name = "ioc_partial_fill",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100,  5, Side::Sell},
        AddOrder{Limit, IOC, 2, 100, 10, Side::Buy},    // fills 5, cancels leftover 5
    },
    .expected_trades = {
        {.buyer_id=2, .seller_id=1, .price=100, .quantity=5},
    },
    .expected_levels = {},
};

// ── 7. IOC no fill ────────────────────────────────────────────────────────────
// When no matching liquidity exists the IOC is cancelled entirely.
static const OrderBookTestCase tc_ioc_no_fill {
    .name = "ioc_no_fill",
    .inputs = {
        AddOrder{Limit, IOC, 1, 100, 10, Side::Buy},    // nothing to match against
    },
    .expected_trades = {},
    .expected_levels = {},
};

// ── 8. FOK rejected ───────────────────────────────────────────────────────────
// FOK is rejected when available ask liquidity is less than the order qty.
// The resting ask is untouched; no trade occurs.
static const OrderBookTestCase tc_fok_rejected {
    .name = "fok_rejected",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},
        AddOrder{Limit, FOK, 2, 100, 15, Side::Buy},    // needs 15, only 10 available → rejected
    },
    .expected_trades = {},
    .expected_levels = {
        {.side=Side::Sell, .price=100, .total_quantity=10, .order_count=1},
    },
};

// ── 9. FOK accepted ───────────────────────────────────────────────────────────
// FOK succeeds when ask liquidity across levels exactly covers the qty.
// Both ask levels are swept; book is empty afterward.
static const OrderBookTestCase tc_fok_accepted {
    .name = "fok_accepted",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},
        AddOrder{Limit, GTC, 2, 105,  5, Side::Sell},
        AddOrder{Limit, FOK, 3, 105, 15, Side::Buy},    // 10@100 + 5@105 = 15 → fills
    },
    .expected_trades = {
        {.buyer_id=3, .seller_id=1, .price=100, .quantity=10},
        {.buyer_id=3, .seller_id=2, .price=105, .quantity=5},
    },
    .expected_levels = {},
};

// ── 10. Sell sweep bids ───────────────────────────────────────────────────────
// A sell order sweeps two bid levels in descending price order (highest bid
// first). The lower bid is only partially consumed; its residual stays.
static const OrderBookTestCase tc_sell_sweep_bids {
    .name = "sell_sweep_bids",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 15, Side::Buy},
        AddOrder{Limit, GTC, 2,  90, 10, Side::Buy},
        AddOrder{Limit, GTC, 3,  85, 20, Side::Sell},   // crosses both bids; 15+5=20
    },
    .expected_trades = {
        {.buyer_id=1, .seller_id=3, .price=100, .quantity=15},
        {.buyer_id=2, .seller_id=3, .price=90,  .quantity=5},
    },
    .expected_levels = {
        {.side=Side::Buy, .price=90, .total_quantity=5, .order_count=1},
    },
};

// ── 11. GTC partial fill rests ────────────────────────────────────────────────
// A GTC buy larger than the resting ask fills what's available, then the
// unfilled remainder rests on the bid side.
static const OrderBookTestCase tc_gtc_partial_fill_rests {
    .name = "gtc_partial_fill_rests",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100,  5, Side::Sell},
        AddOrder{Limit, GTC, 2, 100, 12, Side::Buy},    // fills 5, leftover 7 rests as bid
    },
    .expected_trades = {
        {.buyer_id=2, .seller_id=1, .price=100, .quantity=5},
    },
    .expected_levels = {
        {.side=Side::Buy, .price=100, .total_quantity=7, .order_count=1},
    },
};

// ── 12. IOC sell ──────────────────────────────────────────────────────────────
// IOC sell fills against a resting bid; unfilled remainder is cancelled.
static const OrderBookTestCase tc_ioc_sell {
    .name = "ioc_sell",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100,  8, Side::Buy},
        AddOrder{Limit, IOC, 2, 100, 15, Side::Sell},   // fills 8, cancels leftover 7
    },
    .expected_trades = {
        {.buyer_id=1, .seller_id=2, .price=100, .quantity=8},
    },
    .expected_levels = {},
};

// ── 13. FOK sell rejected ─────────────────────────────────────────────────────
// FOK sell rejected because total bid liquidity is less than the order qty.
static const OrderBookTestCase tc_fok_sell_rejected {
    .name = "fok_sell_rejected",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Buy},
        AddOrder{Limit, FOK, 2, 100, 15, Side::Sell},   // needs 15, only 10 available
    },
    .expected_trades = {},
    .expected_levels = {
        {.side=Side::Buy, .price=100, .total_quantity=10, .order_count=1},
    },
};

// ── 14. FOK sell accepted ─────────────────────────────────────────────────────
// FOK sell sweeps two bid levels when total bid qty exactly covers it.
static const OrderBookTestCase tc_fok_sell_accepted {
    .name = "fok_sell_accepted",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Buy},
        AddOrder{Limit, GTC, 2,  90,  5, Side::Buy},
        AddOrder{Limit, FOK, 3,  90, 15, Side::Sell},   // 10+5=15 → fills
    },
    .expected_trades = {
        {.buyer_id=1, .seller_id=3, .price=100, .quantity=10},
        {.buyer_id=2, .seller_id=3, .price=90,  .quantity=5},
    },
    .expected_levels = {},
};

// ── 15. Cancel then rematch ───────────────────────────────────────────────────
// After cancelling a resting bid, a new buy order correctly fills against
// the existing ask — verifying the book is consistent post-cancel.
static const OrderBookTestCase tc_cancel_then_rematch {
    .name = "cancel_then_rematch",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},
        AddOrder{Limit, GTC, 2,  90,  5, Side::Buy},    // rests; does not cross ask
        CancelOrder{2},
        AddOrder{Limit, GTC, 3, 100, 10, Side::Buy},    // crosses ask 1
    },
    .expected_trades = {
        {.buyer_id=3, .seller_id=1, .price=100, .quantity=10},
    },
    .expected_levels = {},
};

// ── 16. Multiple resting levels both sides ────────────────────────────────────
// Non-crossing orders on both sides all rest; no trades generated.
// Verifies bid/ask level counts and quantities simultaneously.
static const OrderBookTestCase tc_multiple_resting_levels {
    .name = "multiple_resting_levels",
    .inputs = {
        AddOrder{Limit, GTC, 1, 110, 10, Side::Sell},
        AddOrder{Limit, GTC, 2, 120,  5, Side::Sell},
        AddOrder{Limit, GTC, 3,  90,  8, Side::Buy},
        AddOrder{Limit, GTC, 4,  80, 12, Side::Buy},
    },
    .expected_trades = {},
    .expected_levels = {
        {.side=Side::Sell, .price=110, .total_quantity=10, .order_count=1},
        {.side=Side::Sell, .price=120, .total_quantity=5,  .order_count=1},
        {.side=Side::Buy,  .price=90,  .total_quantity=8,  .order_count=1},
        {.side=Side::Buy,  .price=80,  .total_quantity=12, .order_count=1},
    },
};

// ── 17. Cancel order ──────────────────────────────────────────────────────────
// Cancelling one of two orders at the same level adjusts quantity and count.
// Cancelling the sole order at a level removes that level entirely.
static const OrderBookTestCase tc_cancel_order {
    .name = "cancel_order",
    .inputs = {
        AddOrder{Limit, GTC, 1, 100, 10, Side::Sell},   // first order at @100
        AddOrder{Limit, GTC, 2, 100,  5, Side::Sell},   // second order at @100
        AddOrder{Limit, GTC, 3,  90, 20, Side::Buy},    // sole bid
        CancelOrder{1},                           // @100 level: qty 15→5, count 2→1
        CancelOrder{3},                           // removes bid level entirely
    },
    .expected_trades = {},
    .expected_levels = {
        {.side=Side::Sell, .price=100, .total_quantity=5, .order_count=1},
    },
};

// ── 18. Market buy full fill ──────────────────────────────────────────────────
// Market buy sweeps the entire resting ask. Execution price is the maker's
// price (100), not a price field on the market order.
static const OrderBookTestCase tc_market_buy_full_fill {
    .name = "market_buy_full_fill",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100, 10, Side::Sell},
        AddOrder{Market, GTC, 2,   0, 10, Side::Buy},   // price=0 is ignored for market orders
    },
    .expected_trades = {
        {.buyer_id=2, .seller_id=1, .price=100, .quantity=10},
    },
    .expected_levels = {},
};

// ── 19. Market buy partial fill ───────────────────────────────────────────────
// Only 5 units are available; market order fills what it can and does not rest.
static const OrderBookTestCase tc_market_buy_partial_fill {
    .name = "market_buy_partial_fill",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100,  5, Side::Sell},
        AddOrder{Market, GTC, 2,   0, 10, Side::Buy},   // fills 5, remainder gone (no resting)
    },
    .expected_trades = {
        {.buyer_id=2, .seller_id=1, .price=100, .quantity=5},
    },
    .expected_levels = {},
};

// ── 20. Market buy no liquidity ───────────────────────────────────────────────
// Nothing on the ask side — market order executes nothing and does not rest.
static const OrderBookTestCase tc_market_buy_no_liquidity {
    .name = "market_buy_no_liquidity",
    .inputs = {
        AddOrder{Market, GTC, 1, 0, 10, Side::Buy},
    },
    .expected_trades = {},
    .expected_levels = {},
};

// ── 21. Market sell full fill ─────────────────────────────────────────────────
// Market sell hits the best resting bid at maker price.
static const OrderBookTestCase tc_market_sell_full_fill {
    .name = "market_sell_full_fill",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100, 10, Side::Buy},
        AddOrder{Market, GTC, 2,   0, 10, Side::Sell},
    },
    .expected_trades = {
        {.buyer_id=1, .seller_id=2, .price=100, .quantity=10},
    },
    .expected_levels = {},
};

// ── 22. Market buy multi-level sweep ─────────────────────────────────────────
// Market order sweeps two ask levels regardless of price — no price limit.
// First fills the cheaper level (100), then the more expensive one (110).
static const OrderBookTestCase tc_market_buy_multi_level {
    .name = "market_buy_multi_level",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100,  5, Side::Sell},
        AddOrder{Limit,  GTC, 2, 110,  5, Side::Sell},
        AddOrder{Market, GTC, 3,   0, 10, Side::Buy},   // sweeps both levels
    },
    .expected_trades = {
        {.buyer_id=3, .seller_id=1, .price=100, .quantity=5},
        {.buyer_id=3, .seller_id=2, .price=110, .quantity=5},
    },
    .expected_levels = {},
};

// ── 23. Market sell partial fill ─────────────────────────────────────────────
// Only 5 units of bid liquidity exist; market sell fills what's available
// and the remainder is discarded (market orders never rest).
static const OrderBookTestCase tc_market_sell_partial_fill {
    .name = "market_sell_partial_fill",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100,  5, Side::Buy},
        AddOrder{Market, GTC, 2,   0, 10, Side::Sell},  // fills 5, remainder gone
    },
    .expected_trades = {
        {.buyer_id=1, .seller_id=2, .price=100, .quantity=5},
    },
    .expected_levels = {},
};

// ── 24. Market sell no liquidity ──────────────────────────────────────────────
// Nothing on the bid side — market sell executes nothing and does not rest.
static const OrderBookTestCase tc_market_sell_no_liquidity {
    .name = "market_sell_no_liquidity",
    .inputs = {
        AddOrder{Market, GTC, 1, 0, 10, Side::Sell},
    },
    .expected_trades = {},
    .expected_levels = {},
};

// ── 25. Market sell multi-level sweep ────────────────────────────────────────
// Market sell sweeps two bid levels in descending price order (best bid first).
static const OrderBookTestCase tc_market_sell_multi_level {
    .name = "market_sell_multi_level",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100,  5, Side::Buy},
        AddOrder{Limit,  GTC, 2,  90,  5, Side::Buy},
        AddOrder{Market, GTC, 3,   0, 10, Side::Sell},  // hits @100 first, then @90
    },
    .expected_trades = {
        {.buyer_id=1, .seller_id=3, .price=100, .quantity=5},
        {.buyer_id=2, .seller_id=3, .price=90,  .quantity=5},
    },
    .expected_levels = {},
};

// ── 26. Market buy FIFO within level ─────────────────────────────────────────
// Two resting sells at the same price; market buy must fill the earlier-
// arriving order first (price-time priority).
static const OrderBookTestCase tc_market_buy_fifo {
    .name = "market_buy_fifo",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100,  6, Side::Sell},  // arrives first
        AddOrder{Limit,  GTC, 2, 100,  6, Side::Sell},  // arrives second
        AddOrder{Market, GTC, 3,   0,  8, Side::Buy},   // fills 6 from #1, then 2 from #2
    },
    .expected_trades = {
        {.buyer_id=3, .seller_id=1, .price=100, .quantity=6},
        {.buyer_id=3, .seller_id=2, .price=100, .quantity=2},
    },
    .expected_levels = {
        {.side=Side::Sell, .price=100, .total_quantity=4, .order_count=1},
    },
};

// ── 27. Market sell FIFO within level ────────────────────────────────────────
// Two resting bids at the same price; market sell must fill the earlier-
// arriving order first.
static const OrderBookTestCase tc_market_sell_fifo {
    .name = "market_sell_fifo",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100,  6, Side::Buy},   // arrives first
        AddOrder{Limit,  GTC, 2, 100,  6, Side::Buy},   // arrives second
        AddOrder{Market, GTC, 3,   0,  8, Side::Sell},  // fills 6 from #1, then 2 from #2
    },
    .expected_trades = {
        {.buyer_id=1, .seller_id=3, .price=100, .quantity=6},
        {.buyer_id=2, .seller_id=3, .price=100, .quantity=2},
    },
    .expected_levels = {
        {.side=Side::Buy, .price=100, .total_quantity=4, .order_count=1},
    },
};

// ── 28. Market buy after cancel ───────────────────────────────────────────────
// A resting ask is cancelled; a subsequent market buy fills only against
// the remaining liquidity, confirming the book is consistent post-cancel.
static const OrderBookTestCase tc_market_buy_after_cancel {
    .name = "market_buy_after_cancel",
    .inputs = {
        AddOrder{Limit,  GTC, 1, 100, 10, Side::Sell},
        AddOrder{Limit,  GTC, 2, 100,  5, Side::Sell},
        CancelOrder{1},                                  // remove first ask; only #2 remains
        AddOrder{Market, GTC, 3,   0,  5, Side::Buy},   // fills entirely against #2
    },
    .expected_trades = {
        {.buyer_id=3, .seller_id=2, .price=100, .quantity=5},
    },
    .expected_levels = {},
};

static const OrderBookTestCase tc_all_in_one_market_and_limit_orders {
    .name = "all_in_one_market_and_limit_order",

    .inputs = {
        AddOrder{OrderType::Limit,  TimeInForce::GoodTillCancel, 1, 100, 10, Side::Sell},
        AddOrder{OrderType::Limit,  TimeInForce::GoodTillCancel, 2, 105, 20, Side::Sell},
        AddOrder{OrderType::Limit,  TimeInForce::GoodTillCancel, 3, 110, 30, Side::Sell},

        AddOrder{OrderType::Limit,  TimeInForce::GoodTillCancel, 4, 95, 15, Side::Buy},
        AddOrder{OrderType::Limit,  TimeInForce::GoodTillCancel, 5, 90, 20, Side::Buy},

        AddOrder{OrderType::Market, TimeInForce::ImmediateOrCancel, 6, 0, 25, Side::Buy},
        AddOrder{OrderType::Market, TimeInForce::ImmediateOrCancel, 7, 0, 20, Side::Sell},

        AddOrder{OrderType::Limit,  TimeInForce::GoodTillCancel, 8, 105, 20, Side::Buy},

        AddOrder{OrderType::Market, TimeInForce::ImmediateOrCancel, 9, 0, 40, Side::Sell},

        AddOrder{OrderType::Limit,  TimeInForce::GoodTillCancel, 10, 120, 5, Side::Sell},
    },

    .expected_trades = {
        {.buyer_id=6, .seller_id=1, .price=100, .quantity=10},
        {.buyer_id=6, .seller_id=2, .price=105, .quantity=15},

        {.buyer_id=4, .seller_id=7, .price=95, .quantity=15},
        {.buyer_id=5, .seller_id=7, .price=90, .quantity=5},

        {.buyer_id=8, .seller_id=2, .price=105, .quantity=5},
        {.buyer_id=8, .seller_id=9, .price=105, .quantity=15},
        {.buyer_id=5, .seller_id=9, .price=90, .quantity=15},
    },

    .expected_levels = {
        {Side::Sell, 110, 30, 1},
        {Side::Sell, 120, 5, 1},
    },
};

void data_driven_suite() {
    run_order_book_test(tc_exact_match);
    run_order_book_test(tc_partial_fill);
    run_order_book_test(tc_fifo_same_price);
    run_order_book_test(tc_multi_level_sweep);
    run_order_book_test(tc_gtc_rests_then_matches);
    run_order_book_test(tc_ioc_partial_fill);
    run_order_book_test(tc_ioc_no_fill);
    run_order_book_test(tc_fok_rejected);
    run_order_book_test(tc_fok_accepted);
    run_order_book_test(tc_sell_sweep_bids);
    run_order_book_test(tc_gtc_partial_fill_rests);
    run_order_book_test(tc_ioc_sell);
    run_order_book_test(tc_fok_sell_rejected);
    run_order_book_test(tc_fok_sell_accepted);
    run_order_book_test(tc_cancel_then_rematch);
    run_order_book_test(tc_multiple_resting_levels);
    run_order_book_test(tc_cancel_order);
    run_order_book_test(tc_market_buy_full_fill);
    run_order_book_test(tc_market_buy_partial_fill);
    run_order_book_test(tc_market_buy_no_liquidity);
    run_order_book_test(tc_market_sell_full_fill);
    run_order_book_test(tc_market_buy_multi_level);
    run_order_book_test(tc_market_sell_partial_fill);
    run_order_book_test(tc_market_sell_no_liquidity);
    run_order_book_test(tc_market_sell_multi_level);
    run_order_book_test(tc_market_buy_fifo);
    run_order_book_test(tc_market_sell_fifo);
    run_order_book_test(tc_market_buy_after_cancel);
    run_order_book_test(tc_all_in_one_market_and_limit_orders);
}
void run_one(){
    run_order_book_test(tc_all_in_one_market_and_limit_orders, true);
}

// ── Summary ───────────────────────────────────────────────────────────────────

int data_driven_summary() {
    const int total = dd::pass_count + dd::fail_count;
    std::cout << "\n───────────────────────── Data-Driven Results ─────────────────────────\n";
    std::cout << "  Passed: " << dd::pass_count << " / " << total << "\n";
    if (dd::fail_count > 0) {
        std::cout << "  Failed: " << dd::fail_count << "\n";
        return 1;

    }
    std::cout << "  All tests passed.\n";
    return 0;
}
