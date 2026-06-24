#include <iostream>
#include "order_book.h"
#include "trade.h"
#include <map>

bool OrderBook::is_empty() const {
    return (bids_.empty() && asks_.empty());
}

bool OrderBook::has_bids() const {
    return !bids_.empty();
}

bool OrderBook::has_asks() const {
    return !asks_.empty();
}

Price OrderBook::best_bid() const {
    if(!has_bids()){
        throw std::runtime_error("Bids are empty");
    }

    return bids_.begin()->second.get_price();
}

Price OrderBook::best_ask() const {
    if(!has_asks()){
        throw std::runtime_error("Asks are empty");
    }

    return asks_.begin()->second.get_price();
}

const TradeHistory& OrderBook::get_trades() const {
    return trades_;
}

bool OrderBook::has_level(Side side, Price price) const {
    if (side == Side::Buy) return bids_.count(price) > 0;
    return asks_.count(price) > 0;
}

Quantity OrderBook::level_quantity(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it != bids_.end() ? it->second.get_total_quantity() : 0;
    }
    auto it = asks_.find(price);
    return it != asks_.end() ? it->second.get_total_quantity() : 0;
}

std::size_t OrderBook::level_order_count(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it != bids_.end() ? it->second.get_order_count() : 0;
    }
    auto it = asks_.find(price);
    return it != asks_.end() ? it->second.get_order_count() : 0;
}

std::size_t OrderBook::bid_level_count() const { return bids_.size(); }
std::size_t OrderBook::ask_level_count() const { return asks_.size(); }

bool OrderBook::can_fully_fill(const Order& order) const {
    if(order.is_buy()){
        return can_fully_fill_against_side(asks_, order, cmp_buy);
    }
    return can_fully_fill_against_side(bids_, order, cmp_sell);
}

AddOrderResult OrderBook::add_limit_order(Order&& order){
    const TimeInForce tif = order.get_time_in_force();
    
    if(tif == TimeInForce::FillOrKill && !can_fully_fill(order)){
        return AddOrderResult{
            .status_ = AddOrderStatus::Rejected,
            .execution_quantity_ = 0,
            .remaining_quantity_ = order.get_remaining_quantity()
        };
    }
    
    Quantity execution_quantity;
    // std::cout << order.get_order_id() << std::endl;

    if(order.is_buy()){
        execution_quantity = match_buy(order);
    }
    else{
        execution_quantity = match_sell(order);
    }

    Quantity remaining_quantity = order.get_remaining_quantity();
    
    if(tif == TimeInForce::GoodTillCancel){
        if(order.is_buy()){
            return AddOrderResult::from_match(
                add_order_to_side(bids_, std::move(order)),
                order.get_order_type(),
                execution_quantity,
                remaining_quantity
            );
        }
        return AddOrderResult::from_match(
            add_order_to_side(asks_, std::move(order)),
            order.get_order_type(),
            execution_quantity,
            remaining_quantity
        );
    }
    
    return AddOrderResult::from_match(
        order.get_order_type(),
        execution_quantity, 
        remaining_quantity
    );
}

AddOrderResult OrderBook::add_market_order(Order&& order){
    //buy 
    Quantity execution_quantity;

    if(order.is_buy()){
        execution_quantity = match_market_order_against_side(asks_, order);
    }
    else{
        execution_quantity = match_market_order_against_side(bids_, order);
    }

    return AddOrderResult::from_match(
        order.get_order_type(),
        execution_quantity,
        order.get_remaining_quantity()
    );
}
bool OrderBook::remove_look_up(OrderId order_id){
    return order_look_up_.erase(order_id);
}

AddOrderResult OrderBook::add_order(Order order){ //need improvement
    if(order.is_limit_order()){
        return add_limit_order(std::move(order));
    }
    return add_market_order(std::move(order));
}

Quantity OrderBook::match_buy(Order& buy_order){ //need improvement
    return match_limit_order_against_side(buy_order, asks_, cmp_buy);
}

Quantity OrderBook::match_sell(Order& sell_order){ //need improvement
    return match_limit_order_against_side(sell_order, bids_, cmp_sell);
}

Quantity OrderBook::execute_trade(Order& incoming_order, PriceLevel& counterpart_level){
    //execute min quantity of two
    Quantity execution_quantity = std::min(
        incoming_order.get_remaining_quantity(),
        counterpart_level.front_order().get_remaining_quantity()
    );

    Trade trade = Trade::from_match(incoming_order, counterpart_level.front_order());

    //add Trade event
    add_event(TradeExecuted{
        .buyer_id = trade.get_buyer().order_id_,
        .seller_id = trade.get_seller().order_id_,
        .price = trade.get_execution_price(),
        .quantity = trade.get_execution_quantity(),
        .aggressive_side = trade.get_aggressor_side()
    });

    //add trades
    trades_.push_back(trade); //need improvment: preallocate (kafka)

    //front order getting filled should be removed out of lookup
    bool getFilled = (counterpart_level.front_order().get_remaining_quantity() == execution_quantity);
    OrderId front_order_id = counterpart_level.front_order().get_order_id();

    incoming_order.fill_order(execution_quantity);
    counterpart_level.fill_front_order(execution_quantity);

    if(getFilled) remove_look_up(front_order_id);

    return execution_quantity;
}

bool OrderBook::cancel_order(OrderId order_id){
    auto it = order_look_up_.find(order_id);
    if(it == order_look_up_.end()){
        return false;
    }
    
    OrderLocation order_location = it->second;
    const Side& side = order_location.side_;

    order_look_up_.erase(it);
    
    if(side == Side::Buy){
        return cancel_order_side(bids_, std::move(order_location));
    }
    
    return cancel_order_side(asks_, std::move(order_location));
}

MaybeOrderRef OrderBook::find_order(OrderId order_id) const {
    const auto it = order_look_up_.find(order_id);
    if(it == order_look_up_.end()){
        return std::nullopt;
    }

    const OrderLocation& order_location = it->second;
    return *order_location.order_it_;

}

bool OrderBook::modify_order(OrderId order_id, Price new_price, Quantity new_quantity){
    const auto& maybe_old_order = find_order(order_id);
    if(!maybe_old_order.has_value()){
        return false;
    }

    const Order& old_order = maybe_old_order.value().get();
    Order new_order = Order::from_replacement(old_order, new_price, new_quantity);

    cancel_order(order_id);

    AddOrderResult res = add_order(std::move(new_order));

    return res.status_ != AddOrderStatus::Failed;
}


void OrderBook::process(const AddOrder& add_order_msg){
    AddOrderResult res = add_order(Order(add_order_msg));

    OrderId id = add_order_msg.order_id;

    if(res.status_ == AddOrderStatus::Failed || res.status_ == AddOrderStatus::Rejected){
        add_event(OrderRejected{
            .order_id = id,
            .reason = "No reason"
        });
        return;
    }

    add_event(OrderAccepted{.order_id = id});
}

void OrderBook::process(const ModifyOrder& modify_order_msg){
    
    auto [id, new_price, new_quantity] = modify_order_msg;

    bool modified = modify_order(
        modify_order_msg.id,
        modify_order_msg.new_price,
        modify_order_msg.new_quantity
    );
    
    if(!modified){
        add_event(ModifyRejected{
            .order_id = id,
            .reason = "Modify failed"
        });
        return;
    }
    

    add_event(OrderModified{
        .order_id = id,
        .new_price = new_price,
        .new_quantity = new_quantity
    });
}

void OrderBook::process(const CancelOrder& cancel_order_msg){
    bool cancelled = cancel_order(cancel_order_msg.id);
    
    OrderId id = cancel_order_msg.id;
    if(!cancelled){
        add_event(OrderRejected{
            .order_id = id,
            .reason = "Cancel failed"
        });
        return;
    }

    add_event(OrderCanceled{
        .order_id = id
    });

}

void OrderBook::process_message(const Message& msg){
    std::visit([this](auto&& action){
            process(action);
        },
        msg
    );
}

bool OrderBook::add_event(Event event){
    bool added = ring_buffer_.push(std::move(event));
    if(!added){
        drop_events_count_++;
    }
    return added;
}

bool OrderBook::pop_event(Event& storage) {
    bool popped = ring_buffer_.pop(storage);
    return popped;
}

bool OrderBook::has_event() const {
    return !ring_buffer_.empty();
}

// void OrderBook::add_event(const Event& event){
//     //need lock?
//     {
//         std::lock_guard<std::mutex> lock(event_mtx_);
//         event_queue_.push(event);
//         // std::visit(
//         //     [](auto&& ev){
//         //         std::cout << ev << "\n";
//         //     },
//         //     event
//         // );
//     }
//     event_cv_.notify_one();
// }

// MaybeEvent OrderBook::wait_and_pop_event(AtomicBool& running){
//     std::unique_lock<std::mutex> lock(event_mtx_); //need improvement: use lock-free queue

//     event_cv_.wait(
//         lock,
//         [&]{
//             return !event_queue_.empty() || !running;
//         }
//     );

//     if(event_queue_.empty()){
//         return std::nullopt;
//     }

//     Event ev = std::move(event_queue_.front());
//     event_queue_.pop();

//     return ev;
// }

// void OrderBook::notify_events_shutdown() {
//     event_cv_.notify_all();
// }


//debug method

std::string OrderBook::to_string() const {
    std::string output = "================ ORDER BOOK ================\n";
    output += "Asks (Sellers):\n";
    output += levels_to_string(asks_);
    output += "Bids (Buyers):\n";
    output += levels_to_string(bids_);
    output += "============================================\n";

    return output;
}

std::string OrderBook::trades_to_string() const {
    std::string output = "";
    for(const Trade& trade : trades_){
        output += trade.to_string();
    }

    return output; 
}

std::ostream& operator << (std::ostream& os, const OrderBook& order_book) {
    os << order_book.to_string();
    return os;
}

std::ostream& operator << (std::ostream& os, const AddOrderResult& result) {
    os << result.to_string();
    return os;
}
