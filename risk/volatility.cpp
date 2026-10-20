#include "volatility.hpp"

#include <cmath>
#include <stdexcept>

void VolatilityEstimator::update(Price price) {
    if (price <= 0) throw std::invalid_argument("volatility price must be positive");
    if (previous_price_ != 0) {
        const double log_return = std::log(static_cast<double>(price) / previous_price_);
        variance_ = decay_ * variance_ + (1.0 - decay_) * log_return * log_return;
        ++observations_;
    }
    previous_price_ = price;
}

double VolatilityEstimator::volatility() const {
    return std::sqrt(variance_);
}
