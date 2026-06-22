#pragma once

#include "order_book.h"

void consume_events(OrderBook& book, AtomicBool& running);