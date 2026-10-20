#include "../exchange/exchange.hpp"
#include "../market_data/publisher.hpp"
#include "../networking/udp_multicast.hpp"
#include "../trader/trading_engine.hpp"

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
std::string_view status_name(OrderStatus status) {
    switch (status) {
        case OrderStatus::Accepted: return "ACCEPTED";
        case OrderStatus::PartiallyFilled: return "PARTIAL";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
    }
    return "UNKNOWN";
}

void replay(std::vector<MarketDataEvent> events, TradingEngine& trader) {
    for (const auto& event : events) trader.on_market_data(event);
}

void print_reports(const std::vector<ExecutionReport>& reports) {
    for (const auto& report : reports) {
        std::cout << "  order=" << report.order_id << " trader=" << report.trader_id
                  << " status=" << status_name(report.status)
                  << " last=" << report.last_quantity << '@' << report.last_price
                  << " leaves=" << report.leaves_quantity;
        if (!report.reason.empty()) std::cout << " (" << report.reason << ')';
        std::cout << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    constexpr SymbolId symbol = 1;
    RecordingPublisher tape;
    std::unique_ptr<UdpMulticastPublisher> multicast;
    std::unique_ptr<UdpMulticastSubscriber> subscriber;
    std::unique_ptr<FanoutPublisher> fanout;
    IMarketDataPublisher* publisher = &tape;

    if (argc > 1 && std::string_view(argv[1]) == "--udp") {
        subscriber = std::make_unique<UdpMulticastSubscriber>("239.255.42.99", 39001);
        subscriber->open(); // Join before the exchange sends the first packet.
        multicast = std::make_unique<UdpMulticastPublisher>("239.255.42.99", 39001);
        fanout = std::make_unique<FanoutPublisher>(
            std::vector<std::reference_wrapper<IMarketDataPublisher>>{std::ref(tape), std::ref(*multicast)});
        publisher = fanout.get();
        std::cout << "UDP multicast enabled on 239.255.42.99:39001\n";
    }

    Exchange exchange(*publisher);
    MarketMakerStrategy strategy(MarketMakerConfig{
        .symbol = symbol, .clip_size = 25, .minimum_half_spread_ticks = 1,
        .volatility_spread_multiplier = 4.0, .inventory_skew_ticks_per_unit = 0.05});
    RiskLimits limits;
    limits.max_order_notional = 500'000;
    limits.max_absolute_position = 100;
    TradingEngine market_maker(900, limits, strategy);

    // Establish public liquidity, then let the market maker consume that feed
    // and quote inside its spread.
    exchange.submit(Order{1, 101, now_ns(), 9'990, 200, symbol, Side::Buy,
                          OrderType::Limit, TimeInForce::Gtc});
    exchange.submit(Order{2, 102, now_ns(), 10'010, 200, symbol, Side::Sell,
                          OrderType::Limit, TimeInForce::Gtc});
    replay(tape.drain(), market_maker);

    std::cout << "Market-maker quote reports:\n";
    print_reports(market_maker.quote([&exchange](const Order& order) {
        return exchange.submit(order).reports;
    }));
    replay(tape.drain(), market_maker);

    std::cout << "Aggressive market order lifecycle:\n";
    const auto taker = exchange.submit(Order{500, 777, now_ns(), 0, 15, symbol, Side::Buy,
                                             OrderType::Market, TimeInForce::Ioc});
    print_reports(taker.reports);
    for (const auto& report : taker.reports) market_maker.on_execution(report);
    replay(tape.drain(), market_maker);

    // Cancel the still-resting bid, exercising the final lifecycle transition.
    const auto cancelled = exchange.cancel(CancelRequest{1'000'000, 900, symbol, now_ns()});
    for (const auto& report : cancelled.reports) market_maker.on_execution(report);
    replay(tape.drain(), market_maker);

    const auto bid = exchange.best_bid();
    const auto ask = exchange.best_ask();
    std::cout << "Final BBO: " << (bid ? std::to_string(*bid) : "-") << " / "
              << (ask ? std::to_string(*ask) : "-") << '\n';
    std::cout << "Market-maker position: " << market_maker.risk().position(symbol).quantity
              << ", realized PnL: " << static_cast<double>(
                  market_maker.risk().position(symbol).realized_pnl) << '\n';

    if (subscriber) {
        const auto packet = subscriber->receive_for(std::chrono::milliseconds(200));
        std::cout << (packet ? "Received a multicast market-data packet.\n"
                             : "No loopback multicast packet was received.\n");
    }
    return 0;
}
