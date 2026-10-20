// Public, sequenced order-by-order market-data event.
#pragma once

#include "types.hpp"

enum class MarketDataType : std::uint8_t {
    Add = 0,
    Modify = 1,
    Cancel = 2,
    Trade = 3
};

struct MarketDataEvent {
    SequenceNumber sequence{};
    Timestamp timestamp{};
    OrderId order_id{};            // book order for Add/Modify/Cancel; buy order for Trade
    OrderId match_order_id{};      // sell order for Trade, zero otherwise
    Price price{};
    Quantity quantity{};
    SymbolId symbol{};
    Side side{Side::Buy};          // resting side for book events; aggressor side for trades
    MarketDataType type{MarketDataType::Add};
};
