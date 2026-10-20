// Order-entry and execution-report messages.
#pragma once

#include "types.hpp"

#include <string>

struct Order {
    OrderId order_id{};
    TraderId trader_id{};
    Timestamp timestamp{};
    Price price{};                 // ignored for market orders
    Quantity quantity{};           // remaining quantity while in an order book
    SymbolId symbol{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce time_in_force{TimeInForce::Gtc};
};

struct CancelRequest {
    OrderId order_id{};
    TraderId trader_id{};
    SymbolId symbol{};
    Timestamp timestamp{};
};

// A same-price size decrease retains queue priority. A price change or size
// increase is implemented as cancel/re-enter and therefore loses priority.
struct ReplaceRequest {
    OrderId order_id{};
    TraderId trader_id{};
    SymbolId symbol{};
    Price new_price{};
    Quantity new_quantity{};
    Timestamp timestamp{};
};

enum class OrderStatus : std::uint8_t {
    Accepted,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

struct ExecutionReport {
    std::uint64_t execution_id{};
    OrderId order_id{};
    TraderId trader_id{};
    SymbolId symbol{};
    Side side{Side::Buy};
    OrderStatus status{OrderStatus::Accepted};
    Price last_price{};
    Quantity last_quantity{};
    Quantity cumulative_quantity{};
    Quantity leaves_quantity{};
    Timestamp timestamp{};
    std::string reason{};
};
