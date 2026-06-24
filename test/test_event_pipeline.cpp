#include <iostream>
#include <thread>
#include <vector>
#include "order_book.h"
#include "order_messages.h"
#include "event_consumer.h"

#define CHECK(expr)                                          \
    do {                                                     \
        if (!(expr)) {                                       \
            std::cerr << "FAILED: " << #expr                 \
                      << " at " << __FILE__                  \
                      << ":" << __LINE__ << "\n";            \
            return false;                                    \
        }                                                    \
    } while (false)

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool pop_expect(OrderBook& book, auto&& checker) {
    Event ev;
    if (!book.pop_event(ev)) return false;
    return std::visit(checker, ev);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

bool test_add_order_emits_accepted()
{
    OrderBook book;
    book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, 1, 100, 10, Side::Buy});

    CHECK(book.has_event());

    Event ev;
    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<OrderAccepted>(ev));
    CHECK(std::get<OrderAccepted>(ev).order_id == 1);

    return true;
}

bool test_cancel_order_emits_canceled()
{
    OrderBook book;
    book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, 1, 100, 10, Side::Buy});

    Event ev;
    book.pop_event(ev);   // consume the accepted event

    book.process_message(CancelOrder{1});

    CHECK(book.has_event());
    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<OrderCanceled>(ev));
    CHECK(std::get<OrderCanceled>(ev).order_id == 1);

    return true;
}

bool test_cancel_unknown_order_emits_rejected()
{
    OrderBook book;
    book.process_message(CancelOrder{999});

    Event ev;
    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<OrderRejected>(ev));

    return true;
}

bool test_matching_trade_emits_trade_executed()
{
    OrderBook book;
    Event ev;

    // Resting ask → emits OrderAccepted{1}
    book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, 1, 100, 10, Side::Sell});
    book.pop_event(ev);
    CHECK(std::holds_alternative<OrderAccepted>(ev));
    CHECK(std::get<OrderAccepted>(ev).order_id == 1);

    // Aggressing buy → emits TradeExecuted first, then OrderAccepted{2}
    book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, 2, 100, 10, Side::Buy});

    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<TradeExecuted>(ev));
    const auto& trade = std::get<TradeExecuted>(ev);
    CHECK(trade.price == 100);
    CHECK(trade.quantity == 10);

    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<OrderAccepted>(ev));
    CHECK(std::get<OrderAccepted>(ev).order_id == 2);

    return true;
}

bool test_event_fifo_order()
{
    OrderBook book;

    book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, 1, 100, 10, Side::Buy});
    book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, 2, 90,  10, Side::Buy});
    book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, 3, 110, 10, Side::Sell});

    Event ev;

    // First two should be OrderAccepted for order 1 and 2
    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<OrderAccepted>(ev));
    CHECK(std::get<OrderAccepted>(ev).order_id == 1);

    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<OrderAccepted>(ev));
    CHECK(std::get<OrderAccepted>(ev).order_id == 2);

    CHECK(book.pop_event(ev));
    CHECK(std::holds_alternative<OrderAccepted>(ev));
    CHECK(std::get<OrderAccepted>(ev).order_id == 3);

    return true;
}

bool test_concurrent_consumer()
{
    OrderBook book;
    AtomicBool running{true};
    std::vector<Event> received;
    std::mutex received_mtx;

    std::thread consumer([&]() {
        Event ev;
        while (running || book.has_event()) {
            if (book.pop_event(ev)) {
                std::lock_guard<std::mutex> lock(received_mtx);
                received.push_back(ev);
            }
        }
    });

    // Producer: submit 10 orders on the main thread
    for (int i = 1; i <= 10; ++i) {
        book.process_message(AddOrder{OrderType::Limit, TimeInForce::GoodTillCancel, (OrderId)i, 100, 10, Side::Buy});
    }

    running = false;
    consumer.join();

    CHECK((int)received.size() == 10);
    for (const auto& e : received) {
        CHECK(std::holds_alternative<OrderAccepted>(e));
    }

    return true;
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main()
{
    int passed = 0;
    int total  = 0;

    auto run = [&](const char* name, bool (*test)()) {
        ++total;
        if (test()) {
            ++passed;
            std::cout << "[PASS] " << name << "\n";
        } else {
            std::cout << "[FAIL] " << name << "\n";
        }
    };

    run("add_order_emits_accepted",          test_add_order_emits_accepted);
    run("cancel_order_emits_canceled",       test_cancel_order_emits_canceled);
    run("cancel_unknown_emits_rejected",     test_cancel_unknown_order_emits_rejected);
    run("matching_trade_emits_executed",     test_matching_trade_emits_trade_executed);
    run("event_fifo_order",                  test_event_fifo_order);
    run("concurrent_consumer",               test_concurrent_consumer);

    std::cout << passed << "/" << total << " tests passed\n";
    return passed == total ? 0 : 1;
}
