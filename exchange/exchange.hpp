#pragma once

#include "../common/market_data.hpp"
#include "../common/order.hpp"
#include "../common/trade.hpp"
#include "../market_data/publisher.hpp"
#include "order_book.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

struct ExchangeResult {
    std::vector<ExecutionReport> reports;
    std::vector<Trade> trades;
};

// Owns a single-symbol central limit order book and turns order-entry actions
// into execution reports plus an externally sequenced market-data feed.
class Exchange {
public:
    explicit Exchange(IMarketDataPublisher& publisher) : publisher_(publisher) {}

    ExchangeResult submit(Order order);
    ExchangeResult cancel(const CancelRequest& request);
    ExchangeResult replace(const ReplaceRequest& request);

    [[nodiscard]] std::optional<Price> best_bid() const { return book_.best_bid(); }
    [[nodiscard]] std::optional<Price> best_ask() const { return book_.best_ask(); }
    [[nodiscard]] std::size_t resting_orders() const { return book_.order_count(); }

private:
    struct OrderState {
        Order order;
        Quantity cumulative{};
        Quantity leaves{};
        bool active{};
    };

    OrderBook book_;
    IMarketDataPublisher& publisher_;
    SequenceNumber next_sequence_{1};
    std::uint64_t next_execution_id_{1};
    std::unordered_map<OrderId, OrderState> states_;

    ExecutionReport make_report(const OrderState& state, OrderStatus status,
                                Quantity last_quantity, Price last_price,
                                Timestamp timestamp, std::string reason = {});
    ExecutionReport reject(const Order& order, std::string reason);
    void publish_book_change(const BookChange& change, Timestamp timestamp);
    void publish_trade(const Trade& trade);
    void apply_fill(OrderId id, Quantity quantity, Price price, Timestamp timestamp,
                    std::vector<ExecutionReport>& reports);
    void process_match(const MatchResult& match, Timestamp timestamp, ExchangeResult& result);
    bool is_valid(const Order& order, std::string& reason) const;
};
