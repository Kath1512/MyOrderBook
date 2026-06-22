#pragma once
//add order event
//cancel order event
//modify order event
//Trade executed Event

#include "types.h"
#include <string>
#include <variant>
#include <ostream>
#include <format>

struct OrderAccepted {
    OrderId order_id;
};

struct OrderRejected {
    OrderId order_id;
    std::string reason;
};

struct OrderCanceled {
    OrderId order_id;
};

struct CancelRejected {
    OrderId order_id;
    std::string reason;
};

struct OrderModified {
    OrderId order_id;
    Price new_price;
    Quantity new_quantity;
};

struct ModifyRejected {
    OrderId order_id;
    std::string reason;
};

struct TradeExecuted {
    OrderId buyer_id;
    OrderId seller_id;
    Price price;
    Quantity quantity;
    Side aggressive_side;
};

std::ostream& operator << (std::ostream& os, const OrderAccepted& ev);

std::ostream& operator << (std::ostream& os, const OrderRejected& ev);

std::ostream& operator << (std::ostream& os, const OrderCanceled& ev);

std::ostream& operator << (std::ostream& os, const CancelRejected& ev);

std::ostream& operator << (std::ostream& os, const OrderModified& ev);

std::ostream& operator << (std::ostream& os, const ModifyRejected& ev);

std::ostream& operator << (std::ostream& os, const TradeExecuted& ev);

using Event = std::variant<
    OrderAccepted,
    OrderRejected,
    OrderCanceled,
    CancelRejected,
    OrderModified,
    ModifyRejected,
    TradeExecuted
>;