#include "exchange.hpp"

#include <stdexcept>

bool Exchange::is_valid(const Order& order, std::string& reason) const {
    if (order.order_id == 0) {
        reason = "order id must be non-zero";
        return false;
    }
    if (order.trader_id == 0) {
        reason = "trader id must be non-zero";
        return false;
    }
    if (order.quantity == 0) {
        reason = "quantity must be positive";
        return false;
    }
    if (order.type == OrderType::Limit && order.price <= 0) {
        reason = "limit price must be positive";
        return false;
    }
    return true;
}

ExecutionReport Exchange::make_report(const OrderState& state, OrderStatus status,
                                      Quantity last_quantity, Price last_price,
                                      Timestamp timestamp, std::string reason) {
    return ExecutionReport{
        next_execution_id_++, state.order.order_id, state.order.trader_id, state.order.symbol,
        state.order.side, status, last_price, last_quantity, state.cumulative, state.leaves,
        timestamp, std::move(reason)};
}

ExecutionReport Exchange::reject(const Order& order, std::string reason) {
    return ExecutionReport{
        next_execution_id_++, order.order_id, order.trader_id, order.symbol, order.side,
        OrderStatus::Rejected, 0, 0, 0, order.quantity, now_ns(), std::move(reason)};
}

void Exchange::publish_book_change(const BookChange& change, Timestamp timestamp) {
    MarketDataType type = MarketDataType::Add;
    if (change.type == BookChangeType::Reduced) type = MarketDataType::Modify;
    if (change.type == BookChangeType::Removed) type = MarketDataType::Cancel;

    publisher_.publish(MarketDataEvent{
        next_sequence_++, timestamp, change.order.order_id, 0, change.order.price,
        change.type == BookChangeType::Removed ? 0U : change.order.quantity,
        change.order.symbol, change.order.side, type});
}

void Exchange::publish_trade(const Trade& trade) {
    publisher_.publish(MarketDataEvent{
        next_sequence_++, trade.timestamp, trade.buy_order_id, trade.sell_order_id,
        trade.price, trade.quantity, trade.symbol, trade.aggressor_side,
        MarketDataType::Trade});
}

void Exchange::apply_fill(OrderId id, Quantity quantity, Price price, Timestamp timestamp,
                          std::vector<ExecutionReport>& reports) {
    auto& state = states_.at(id);
    state.cumulative += quantity;
    state.leaves -= quantity;
    state.active = state.leaves != 0;
    reports.push_back(make_report(
        state, state.leaves == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled,
        quantity, price, timestamp));
}

void Exchange::process_match(const MatchResult& match, Timestamp timestamp, ExchangeResult& result) {
    for (std::size_t i = 0; i < match.trades.size(); ++i) {
        const Trade& trade = match.trades[i];
        result.trades.push_back(trade);
        apply_fill(trade.buy_order_id, trade.quantity, trade.price, trade.timestamp, result.reports);
        apply_fill(trade.sell_order_id, trade.quantity, trade.price, trade.timestamp, result.reports);
        publish_trade(trade);
        publish_book_change(match.changes.at(i), timestamp);
    }
    // The only unmatched change is the remainder that rested on the book.
    for (std::size_t i = match.trades.size(); i < match.changes.size(); ++i) {
        publish_book_change(match.changes[i], timestamp);
    }
}

ExchangeResult Exchange::submit(Order order) {
    ExchangeResult result;
    if (order.timestamp == 0) order.timestamp = now_ns();

    std::string reason;
    if (!is_valid(order, reason) || states_.contains(order.order_id)) {
        if (reason.empty()) reason = "duplicate order id";
        result.reports.push_back(reject(order, std::move(reason)));
        return result;
    }

    states_.emplace(order.order_id, OrderState{order, 0, order.quantity, true});
    auto& state = states_.at(order.order_id);
    result.reports.push_back(make_report(state, OrderStatus::Accepted, 0, 0, order.timestamp));

    try {
        const MatchResult match = book_.add(order);
        process_match(match, order.timestamp, result);

        state.active = match.rested;
        if (!match.rested && state.leaves != 0) {
            state.leaves = 0; // IOC/market remainder is cancelled, never rested.
            result.reports.push_back(make_report(
                state, OrderStatus::Cancelled, 0, 0, order.timestamp,
                "unfilled IOC/market remainder cancelled"));
        }
    } catch (const std::exception& error) {
        state.active = false;
        state.leaves = 0;
        result.reports.push_back(make_report(state, OrderStatus::Rejected, 0, 0,
                                             order.timestamp, error.what()));
    }
    return result;
}

ExchangeResult Exchange::cancel(const CancelRequest& request) {
    ExchangeResult result;
    const Timestamp timestamp = request.timestamp == 0 ? now_ns() : request.timestamp;
    const auto found = states_.find(request.order_id);
    if (found == states_.end() || !found->second.active ||
        found->second.order.trader_id != request.trader_id ||
        found->second.order.symbol != request.symbol) {
        Order rejected{request.order_id, request.trader_id, timestamp, 0, 0, request.symbol};
        result.reports.push_back(reject(rejected, "unknown, completed, or unauthorized order"));
        return result;
    }

    const auto removed = book_.cancel(request.order_id);
    if (!removed) {
        result.reports.push_back(reject(found->second.order, "order is no longer resting"));
        return result;
    }
    found->second.active = false;
    found->second.leaves = 0;
    result.reports.push_back(make_report(found->second, OrderStatus::Cancelled, 0, 0, timestamp));
    publish_book_change(BookChange{BookChangeType::Removed, *removed}, timestamp);
    return result;
}

ExchangeResult Exchange::replace(const ReplaceRequest& request) {
    ExchangeResult result;
    const Timestamp timestamp = request.timestamp == 0 ? now_ns() : request.timestamp;
    const auto found = states_.find(request.order_id);
    if (found == states_.end() || !found->second.active ||
        found->second.order.trader_id != request.trader_id ||
        found->second.order.symbol != request.symbol || request.new_quantity == 0 ||
        request.new_price <= 0) {
        Order rejected{request.order_id, request.trader_id, timestamp, request.new_price,
                       request.new_quantity, request.symbol};
        result.reports.push_back(reject(rejected, "invalid, completed, or unauthorized replace"));
        return result;
    }

    auto& state = found->second;
    const Order resting = *book_.find(request.order_id);
    if (request.new_price == resting.price) {
        const AmendResult amended = book_.amend_quantity(request.order_id, request.new_quantity);
        state.leaves = request.new_quantity;
        state.order.quantity = request.new_quantity;
        if (!amended.after) {
            state.active = false;
            result.reports.push_back(make_report(state, OrderStatus::Cancelled, 0, 0, timestamp));
            publish_book_change(BookChange{BookChangeType::Removed, *amended.before}, timestamp);
            return result;
        }
        result.reports.push_back(make_report(
            state, OrderStatus::Accepted, 0, 0, timestamp,
            amended.priority_lost ? "quantity increased; queue priority reset" : "quantity amended"));
        publish_book_change(BookChange{BookChangeType::Reduced, *amended.after}, timestamp);
        return result;
    }

    // A price change cancels the old resting order and re-enters the same id.
    const auto removed = book_.cancel(request.order_id);
    publish_book_change(BookChange{BookChangeType::Removed, *removed}, timestamp);
    state.order.price = request.new_price;
    state.order.quantity = request.new_quantity;
    state.order.timestamp = timestamp;
    state.leaves = request.new_quantity;
    state.active = true;
    result.reports.push_back(make_report(state, OrderStatus::Accepted, 0, 0, timestamp,
                                         "price changed; queue priority reset"));
    const MatchResult match = book_.add(state.order);
    process_match(match, timestamp, result);
    state.active = match.rested;
    if (!match.rested && state.leaves != 0) {
        state.leaves = 0;
        result.reports.push_back(make_report(state, OrderStatus::Cancelled, 0, 0, timestamp,
                                             "unfilled replacement remainder cancelled"));
    }
    return result;
}
