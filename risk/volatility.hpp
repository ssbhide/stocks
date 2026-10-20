#pragma once

#include "../common/types.hpp"

#include <cstddef>

// EWMA of log-return variance. The output is volatility per observed price
// update; callers decide how to annualize it for their own time convention.
class VolatilityEstimator {
public:
    explicit VolatilityEstimator(double decay = 0.94) : decay_(decay) {}

    void update(Price price);
    [[nodiscard]] double volatility() const;
    [[nodiscard]] std::size_t observations() const { return observations_; }

private:
    double decay_;
    Price previous_price_{};
    double variance_{};
    std::size_t observations_{};
};
