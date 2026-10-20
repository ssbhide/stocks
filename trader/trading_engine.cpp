#include "trading_engine.hpp"

FeedApplyStatus TradingEngine::on_market_data(const MarketDataEvent& event) {
    const auto status = book_.apply(event);
    if (status == FeedApplyStatus::Applied && event.type == MarketDataType::Trade) {
        volatility_.update(event.price);
    }
    return status;
}

std::vector<ExecutionReport> TradingEngine::quote(const OrderSender& send) {
    if (!book_.synchronized()) return {};
    const auto intents = strategy_.make_quotes(
        book_, volatility_, risk_.position(strategy_.symbol()).quantity);
    std::vector<ExecutionReport> reports;
    for (const QuoteIntent& intent : intents) {
        Order order{
            next_order_id_++, trader_id_, now_ns(), intent.price, intent.quantity, strategy_.symbol(),
            intent.side, OrderType::Limit, TimeInForce::Gtc};
        const auto decision = risk_.check(order, book_.mid_price());
        if (!decision.approved) continue;

        auto order_reports = send(order);
        bool accepted = false;
        for (const auto& report : order_reports) {
            if (report.order_id == order.order_id && report.trader_id == trader_id_ &&
                report.status == OrderStatus::Accepted) {
                accepted = true;
                break;
            }
        }
        if (accepted) risk_.on_order_accepted(order);
        for (const auto& report : order_reports) {
            if (report.trader_id == trader_id_) on_execution(report);
            reports.push_back(report);
        }
    }
    return reports;
}

void TradingEngine::on_execution(const ExecutionReport& report) {
    if (report.trader_id == trader_id_) risk_.on_execution(report);
}
