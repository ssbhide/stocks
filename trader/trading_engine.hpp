#pragma once

#include "../market_data/local_book.hpp"
#include "../risk/risk_manager.hpp"
#include "../risk/volatility.hpp"
#include "market_maker.hpp"

#include <functional>
#include <vector>

// Ties market-data consumption, model decisions, risk gates, and order entry
// together without coupling the strategy package to a particular exchange API.
class TradingEngine {
public:
    using OrderSender = std::function<std::vector<ExecutionReport>(const Order&)>;

    TradingEngine(TraderId trader_id, RiskLimits limits, MarketMakerStrategy strategy,
                  OrderId first_order_id = 1'000'000)
        : trader_id_(trader_id), risk_(std::move(limits)), strategy_(std::move(strategy)),
          next_order_id_(first_order_id) {}

    FeedApplyStatus on_market_data(const MarketDataEvent& event);
    std::vector<ExecutionReport> quote(const OrderSender& send);
    void on_execution(const ExecutionReport& report);

    [[nodiscard]] const LocalOrderBook& book() const { return book_; }
    [[nodiscard]] const RiskManager& risk() const { return risk_; }
    [[nodiscard]] const VolatilityEstimator& volatility() const { return volatility_; }

private:
    TraderId trader_id_;
    LocalOrderBook book_;
    RiskManager risk_;
    VolatilityEstimator volatility_;
    MarketMakerStrategy strategy_;
    OrderId next_order_id_;
};
