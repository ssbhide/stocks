// Shared, deliberately integer-only primitives. Prices are expressed in ticks,
// never floating point, so matching and risk checks remain deterministic.
#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

using OrderId = std::uint64_t;
using TraderId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::uint64_t;       // Unix epoch nanoseconds
using Price = std::int64_t;             // integer price ticks
using Quantity = std::uint32_t;
using SymbolId = std::uint32_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : std::uint8_t { Market = 0, Limit = 1 };
enum class TimeInForce : std::uint8_t { Gtc = 0, Ioc = 1 };

inline constexpr Side opposite(Side side) {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

inline constexpr std::string_view to_string(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

inline Timestamp now_ns() {
    using namespace std::chrono;
    return static_cast<Timestamp>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}
