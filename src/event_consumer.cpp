#include "order_book.h"
#include <event_consumer.h>

void consume_events(OrderBook& book, AtomicBool& running) {
    int cnt = 0;
    while (running || book.has_event()) {
        auto ev = book.wait_and_pop_event(running);
        cnt++;
        if (!ev) {
            continue;
        }
        
        std::visit(
            [](auto&& event) {
                std::cout << event << '\n';
            },
            ev.value()
        );
    }
    std::cout << "Total loops: " << cnt << std::endl;
}