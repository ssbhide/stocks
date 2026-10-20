#pragma once

#include "types.hpp"

struct Trade {
    OrderId buy_order_id{};
    OrderId sell_order_id{};
    TraderId buyer_id{};
    TraderId seller_id{};
    Price price{};
    Timestamp timestamp{};
    Quantity quantity{};
    SymbolId symbol{};
    Side aggressor_side{Side::Buy};
};
