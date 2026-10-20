#pragma once

#include "../common/order.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

struct RiskLimits {
    Quantity max_order_quantity{1'000};
    long double max_order_notional{10'000'000};
    std::int64_t max_absolute_position{10'000};
    long double max_gross_notional{25'000'000};
    std::size_t max_open_orders{100};
    long double max_daily_loss{1'000'000};
    std::uint32_t price_collar_bps{500}; // 5% around current mid/reference
};

struct RiskDecision {
    bool approved{};
    std::string reason;
};

struct Position {
    std::int64_t quantity{};
    long double average_price{};
    long double realized_pnl{};
};

class RiskManager {
public:
    explicit RiskManager(RiskLimits limits) : limits_(limits) {}

    [[nodiscard]] RiskDecision check(const Order& order,
                                     std::optional<Price> reference_price) const;
    void on_order_accepted(const Order& order);
    void on_execution(const ExecutionReport& report);
    void set_kill_switch(bool enabled) { kill_switch_ = enabled; }

    [[nodiscard]] const Position& position(SymbolId symbol) const;
    [[nodiscard]] std::size_t open_orders() const { return working_orders_.size(); }
    [[nodiscard]] bool kill_switch() const { return kill_switch_; }

private:
    RiskLimits limits_;
    bool kill_switch_{};
    std::unordered_map<OrderId, Order> working_orders_;
    std::unordered_map<SymbolId, Position> positions_;

    [[nodiscard]] long double total_realized_pnl() const;
    void apply_trade(const ExecutionReport& report);
};
