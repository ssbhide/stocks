#include "order_book.hpp"

#include <algorithm>
#include <stdexcept>

bool OrderBook::crosses(const Order& incoming) const {
    if (incoming.type == OrderType::Market) {
        return incoming.side == Side::Buy ? !asks_.empty() : !bids_.empty();
    }
    if (incoming.side == Side::Buy) {
        return !asks_.empty() && incoming.price >= asks_.begin()->first;
    }
    return !bids_.empty() && incoming.price <= bids_.begin()->first;
}

void OrderBook::rest(Order order) {
    if (order.side == Side::Buy) {
        auto [level, inserted] = bids_.try_emplace(order.price, PriceLevel{order.price, {}});
        level->second.orders.push_back(std::move(order));
        auto position = std::prev(level->second.orders.end());
        locations_.emplace(position->order_id, Location{Side::Buy, level->first, position});
    } else {
        auto [level, inserted] = asks_.try_emplace(order.price, PriceLevel{order.price, {}});
        level->second.orders.push_back(std::move(order));
        auto position = std::prev(level->second.orders.end());
        locations_.emplace(position->order_id, Location{Side::Sell, level->first, position});
    }
}

void OrderBook::erase_location(const Location& location) {
    if (location.side == Side::Buy) {
        auto level = bids_.find(location.price);
        level->second.orders.erase(location.iterator);
        if (level->second.orders.empty()) bids_.erase(level);
    } else {
        auto level = asks_.find(location.price);
        level->second.orders.erase(location.iterator);
        if (level->second.orders.empty()) asks_.erase(level);
    }
}

MatchResult OrderBook::add(Order incoming) {
    if (incoming.quantity == 0 || locations_.contains(incoming.order_id)) {
        throw std::invalid_argument("order must have a positive, unique id and quantity");
    }

    MatchResult result;
    const Quantity original = incoming.quantity;
    while (incoming.quantity != 0 && crosses(incoming)) {
        auto& queue = incoming.side == Side::Buy ? asks_.begin()->second.orders : bids_.begin()->second.orders;
        auto passive = queue.begin();
        const Quantity fill = std::min(incoming.quantity, passive->quantity);

        Trade trade;
        trade.buy_order_id = incoming.side == Side::Buy ? incoming.order_id : passive->order_id;
        trade.sell_order_id = incoming.side == Side::Sell ? incoming.order_id : passive->order_id;
        trade.buyer_id = incoming.side == Side::Buy ? incoming.trader_id : passive->trader_id;
        trade.seller_id = incoming.side == Side::Sell ? incoming.trader_id : passive->trader_id;
        trade.price = passive->price; // maker-price matching
        trade.timestamp = incoming.timestamp;
        trade.quantity = fill;
        trade.symbol = incoming.symbol;
        trade.aggressor_side = incoming.side;
        result.trades.push_back(trade);

        incoming.quantity -= fill;
        passive->quantity -= fill;
        if (passive->quantity == 0) {
            const Order removed = *passive;
            const auto locator = locations_.at(removed.order_id);
            locations_.erase(removed.order_id);
            erase_location(locator);
            result.changes.push_back(BookChange{BookChangeType::Removed, removed});
        } else {
            result.changes.push_back(BookChange{BookChangeType::Reduced, *passive});
        }
    }

    result.executed = original - incoming.quantity;
    result.leaves = incoming.quantity;
    if (incoming.quantity != 0 && incoming.type == OrderType::Limit &&
        incoming.time_in_force == TimeInForce::Gtc) {
        rest(incoming);
        result.rested = true;
        result.changes.push_back(BookChange{BookChangeType::Rested, incoming});
    }
    return result;
}

std::optional<Order> OrderBook::cancel(OrderId id) {
    const auto found = locations_.find(id);
    if (found == locations_.end()) return std::nullopt;
    const Order order = *found->second.iterator;
    const Location location = found->second;
    locations_.erase(found);
    erase_location(location);
    return order;
}

AmendResult OrderBook::amend_quantity(OrderId id, Quantity new_quantity) {
    AmendResult result;
    const auto found = locations_.find(id);
    if (found == locations_.end()) return result;

    result.found = true;
    result.before = *found->second.iterator;
    if (new_quantity == 0) {
        const Location location = found->second;
        locations_.erase(found);
        erase_location(location);
        return result;
    }

    auto& resting = *found->second.iterator;
    if (new_quantity <= resting.quantity) {
        resting.quantity = new_quantity;
        result.after = resting;
        return result;
    }

    // Exchanges generally reset time priority when a resting order increases.
    Order moved = resting;
    moved.quantity = new_quantity;
    const Location location = found->second;
    locations_.erase(found);
    erase_location(location);
    rest(moved);
    result.priority_lost = true;
    result.after = moved;
    return result;
}

std::optional<Order> OrderBook::find(OrderId id) const {
    const auto found = locations_.find(id);
    if (found == locations_.end()) return std::nullopt;
    return *found->second.iterator;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}
