#include "order_book.h"
#include <event_consumer.h>

void consume_events(OrderBook& book, AtomicBool& running) {
    Event item;
    int cnt = 0;
    while (running || book.has_event()) {
        bool success = book.pop_event(item);
        cnt++;

        if(!success) continue;

        std::visit([](auto&& event) {
            std::cout << event << "\n";
        }, item);
    }

    std::cout << "Total loops: " << cnt << "\n";
}