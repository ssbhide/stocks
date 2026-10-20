#include "../exchange/exchange.hpp"
#include "../exchange/order_book.hpp"
#include "../market_data/local_book.hpp"
#include "../market_data/publisher.hpp"
#include "../networking/market_data_codec.hpp"
#include "../risk/risk_manager.hpp"
#include "../risk/volatility.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

Order limit(OrderId id, TraderId trader, Side side, Price price, Quantity quantity) {
    return Order{id, trader, 1, price, quantity, 1, side, OrderType::Limit, TimeInForce::Gtc};
}

void test_price_time_priority() {
    OrderBook book;
    book.add(limit(1, 10, Side::Sell, 101, 10));
    book.add(limit(2, 11, Side::Sell, 101, 10));
    const auto match = book.add(limit(3, 12, Side::Buy, 102, 15));
    check(match.trades.size() == 2, "expected two matches");
    check(match.trades[0].sell_order_id == 1 && match.trades[0].quantity == 10,
          "first resting order must win FIFO");
    check(match.trades[1].sell_order_id == 2 && match.trades[1].quantity == 5,
          "second resting order should receive remainder");
    check(book.find(1) == std::nullopt, "filled order should leave book");
    check(book.find(2)->quantity == 5, "partial passive order should remain");

    OrderBook amended_book;
    amended_book.add(limit(4, 10, Side::Buy, 100, 10));
    amended_book.add(limit(5, 11, Side::Buy, 100, 10));
    check(amended_book.amend_quantity(4, 20).priority_lost, "increase must reset priority");
    const auto second_match = amended_book.add(limit(6, 12, Side::Sell, 100, 5));
    check(second_match.trades[0].buy_order_id == 5, "increased order must go to queue tail");
}

void test_exchange_lifecycle_and_feed() {
    RecordingPublisher publisher;
    Exchange exchange(publisher);
    exchange.submit(limit(1, 10, Side::Sell, 101, 10));
    const auto result = exchange.submit(
        Order{2, 11, 2, 0, 10, 1, Side::Buy, OrderType::Market, TimeInForce::Ioc});
    check(result.trades.size() == 1, "market order should trade");
    check(result.trades[0].price == 101, "trade must execute at maker price");
    check(result.reports.size() == 3, "accept plus buyer/seller fills expected");
    const auto events = publisher.snapshot();
    check(events.size() == 3, "add, trade, and removal events expected");
    check(events[0].type == MarketDataType::Add && events[1].type == MarketDataType::Trade &&
              events[2].type == MarketDataType::Cancel,
          "feed lifecycle ordering is wrong");
    check(events[0].sequence == 1 && events[2].sequence == 3, "feed must be sequenced");

    exchange.submit(limit(3, 20, Side::Buy, 99, 5));
    const auto amended = exchange.replace(ReplaceRequest{3, 20, 1, 99, 3, 3});
    check(amended.reports.size() == 1 && amended.reports[0].status == OrderStatus::Accepted,
          "same-price replace should be accepted");
    const auto cancelled = exchange.cancel(CancelRequest{3, 20, 1, 4});
    check(cancelled.reports.size() == 1 && cancelled.reports[0].status == OrderStatus::Cancelled,
          "owner cancel should complete lifecycle");
    const auto extended_events = publisher.snapshot();
    check(extended_events[4].type == MarketDataType::Modify &&
              extended_events[5].type == MarketDataType::Cancel,
          "replace and explicit cancel must publish book updates");
}

void test_codec_and_local_book() {
    const MarketDataEvent original{99, 123, 1, 2, -10, 3, 7, Side::Sell, MarketDataType::Trade};
    const auto bytes = MarketDataCodec::encode(original);
    const auto decoded = MarketDataCodec::decode(bytes.data(), bytes.size());
    check(decoded && decoded->sequence == 99 && decoded->price == -10 &&
              decoded->match_order_id == 2 && decoded->type == MarketDataType::Trade,
          "codec round-trip failed");

    LocalOrderBook book;
    check(book.apply(MarketDataEvent{1, 0, 11, 0, 100, 10, 1, Side::Buy, MarketDataType::Add}) ==
              FeedApplyStatus::Applied,
          "bid add failed");
    check(book.apply(MarketDataEvent{2, 0, 12, 0, 102, 10, 1, Side::Sell, MarketDataType::Add}) ==
              FeedApplyStatus::Applied,
          "ask add failed");
    check(book.mid_price() == 101, "midpoint incorrect");
    check(book.apply(MarketDataEvent{4, 0, 12, 0, 102, 5, 1, Side::Sell, MarketDataType::Modify}) ==
              FeedApplyStatus::GapDetected,
          "packet gap must stop the book");
    check(!book.synchronized(), "gap should mark feed unsynchronized");
}

void test_risk_and_volatility() {
    RiskLimits limits;
    limits.max_absolute_position = 20;
    limits.price_collar_bps = 50;
    RiskManager risk(limits);
    const auto bad = risk.check(limit(1, 1, Side::Buy, 110, 1), 100);
    check(!bad.approved, "price collar must reject far limit");

    const Order buy = limit(1, 1, Side::Buy, 100, 10);
    risk.on_order_accepted(buy);
    risk.on_execution(ExecutionReport{1, 1, 1, 1, Side::Buy, OrderStatus::Filled,
                                      100, 10, 10, 0, 1, {}});
    const Order sell = limit(2, 1, Side::Sell, 110, 10);
    risk.on_order_accepted(sell);
    risk.on_execution(ExecutionReport{2, 2, 1, 1, Side::Sell, OrderStatus::Filled,
                                      110, 10, 10, 0, 2, {}});
    check(risk.position(1).quantity == 0 && risk.position(1).realized_pnl == 100,
          "position and realized PnL accounting failed");

    VolatilityEstimator volatility;
    volatility.update(100);
    volatility.update(101);
    check(volatility.observations() == 1 && volatility.volatility() > 0.0,
          "volatility estimator failed to record return");
}
} // namespace

int main() {
    try {
        test_price_time_priority();
        test_exchange_lifecycle_and_feed();
        test_codec_and_local_book();
        test_risk_and_volatility();
        std::cout << "All HFT lifecycle tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
