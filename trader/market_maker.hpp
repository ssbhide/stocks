#pragma once

#include "../common/order.hpp"
#include "../market_data/local_book.hpp"
#include "../risk/volatility.hpp"

#include <vector>

struct QuoteIntent {
    Side side{Side::Buy};
    Price price{};
    Quantity quantity{};
};

struct MarketMakerConfig {
    SymbolId symbol{};
    Quantity clip_size{10};
    Price minimum_half_spread_ticks{1};
    double volatility_spread_multiplier{3.0};
    double inventory_skew_ticks_per_unit{0.05};
};

// A deliberately small two-sided market maker: current midpoint is fair value,
// EWMA volatility widens the quote, and inventory shifts both quotes so that a
// long book preferentially sells and a short book preferentially buys.
class MarketMakerStrategy {
public:
    explicit MarketMakerStrategy(MarketMakerConfig config) : config_(config) {}

    [[nodiscard]] std::vector<QuoteIntent> make_quotes(const LocalOrderBook& book,
                                                        const VolatilityEstimator& volatility,
                                                        std::int64_t inventory) const;
    [[nodiscard]] SymbolId symbol() const { return config_.symbol; }

private:
    MarketMakerConfig config_;
};
