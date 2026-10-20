#include "market_maker.hpp"

#include <algorithm>
#include <cmath>

std::vector<QuoteIntent> MarketMakerStrategy::make_quotes(
    const LocalOrderBook& book, const VolatilityEstimator& volatility,
    std::int64_t inventory) const {
    const auto midpoint = book.mid_price();
    if (!midpoint || config_.clip_size == 0) return {};

    const Price volatility_width = static_cast<Price>(
        std::ceil(std::fabs(static_cast<double>(*midpoint)) * volatility.volatility() *
                  config_.volatility_spread_multiplier));
    const Price half_spread = std::max(config_.minimum_half_spread_ticks, volatility_width);
    const Price inventory_skew = static_cast<Price>(std::llround(
        static_cast<double>(inventory) * config_.inventory_skew_ticks_per_unit));
    const Price bid = *midpoint - half_spread - inventory_skew;
    const Price ask = *midpoint + half_spread - inventory_skew;
    if (bid <= 0 || bid >= ask) return {};

    return {
        QuoteIntent{Side::Buy, bid, config_.clip_size},
        QuoteIntent{Side::Sell, ask, config_.clip_size},
    };
}
