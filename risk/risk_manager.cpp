#include "risk_manager.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace {
constexpr Position empty_position{};

long double absolute_notional(std::int64_t quantity, Price price) {
    return std::fabs(static_cast<long double>(quantity) * static_cast<long double>(price));
}
} // namespace

RiskDecision RiskManager::check(const Order& order, std::optional<Price> reference_price) const {
    if (kill_switch_) return {false, "kill switch is active"};
    if (order.quantity == 0 || order.quantity > limits_.max_order_quantity) {
        return {false, "order quantity exceeds limit"};
    }
    if (working_orders_.size() >= limits_.max_open_orders) {
        return {false, "open order count limit reached"};
    }

    const Price price_for_risk = reference_price.value_or(order.price);
    if (price_for_risk <= 0) return {false, "no positive reference price for risk check"};
    const long double order_notional =
        static_cast<long double>(order.quantity) * static_cast<long double>(price_for_risk);
    if (order_notional > limits_.max_order_notional) {
        return {false, "order notional exceeds limit"};
    }
    if (order.type == OrderType::Limit && reference_price && order.price > 0) {
        const long double deviation_bps =
            std::fabs(static_cast<long double>(order.price - *reference_price)) * 10'000.0L /
            static_cast<long double>(*reference_price);
        if (deviation_bps > limits_.price_collar_bps) {
            return {false, "limit price outside collar"};
        }
    }

    const Position& current = position(order.symbol);
    const std::int64_t signed_quantity =
        order.side == Side::Buy ? static_cast<std::int64_t>(order.quantity)
                                : -static_cast<std::int64_t>(order.quantity);
    if (std::llabs(current.quantity + signed_quantity) > limits_.max_absolute_position) {
        return {false, "worst-case position exceeds limit"};
    }

    long double gross = order_notional;
    for (const auto& [symbol, current_position] : positions_) {
        gross += absolute_notional(current_position.quantity, price_for_risk);
    }
    if (gross > limits_.max_gross_notional) return {false, "gross notional exceeds limit"};
    if (-total_realized_pnl() >= limits_.max_daily_loss) return {false, "daily loss limit reached"};
    return {true, {}};
}

void RiskManager::on_order_accepted(const Order& order) {
    working_orders_[order.order_id] = order;
}

void RiskManager::on_execution(const ExecutionReport& report) {
    if (report.last_quantity != 0) apply_trade(report);
    if (report.status == OrderStatus::Filled || report.status == OrderStatus::Cancelled ||
        report.status == OrderStatus::Rejected) {
        working_orders_.erase(report.order_id);
    }
}

const Position& RiskManager::position(SymbolId symbol) const {
    const auto found = positions_.find(symbol);
    return found == positions_.end() ? empty_position : found->second;
}

long double RiskManager::total_realized_pnl() const {
    long double result = 0;
    for (const auto& [symbol, position] : positions_) result += position.realized_pnl;
    return result;
}

void RiskManager::apply_trade(const ExecutionReport& report) {
    auto& position = positions_[report.symbol];
    const std::int64_t delta = report.side == Side::Buy
        ? static_cast<std::int64_t>(report.last_quantity)
        : -static_cast<std::int64_t>(report.last_quantity);
    const std::int64_t previous = position.quantity;
    const std::int64_t next = previous + delta;
    const long double fill_price = report.last_price;

    if (previous == 0 || (previous > 0) == (delta > 0)) {
        const long double prior_notional = std::fabs(static_cast<long double>(previous)) * position.average_price;
        const long double added_notional = std::fabs(static_cast<long double>(delta)) * fill_price;
        position.quantity = next;
        position.average_price = (prior_notional + added_notional) / std::fabs(static_cast<long double>(next));
        return;
    }

    const std::int64_t closed = std::min(std::llabs(previous), std::llabs(delta));
    // Selling a long earns sell - average; buying a short earns average - buy.
    position.realized_pnl += previous > 0
        ? (fill_price - position.average_price) * closed
        : (position.average_price - fill_price) * closed;
    position.quantity = next;
    if (next == 0) {
        position.average_price = 0;
    } else if ((next > 0) != (previous > 0)) {
        // The trade crossed through flat: the residual opened at this fill price.
        position.average_price = fill_price;
    }
}
