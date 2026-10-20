#pragma once

#include "../common/market_data.hpp"

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

struct Level {
    Price price{};
    Quantity quantity{};
};

enum class FeedApplyStatus { Applied, GapDetected, InvalidEvent };

// An aggregated L2 projection reconstructed from the exchange's order-by-order
// multicast feed. It is intentionally not a matching engine: it is the fast
// trader-side view used for BBO, depth, and risk-price references.
class LocalOrderBook {
public:
    FeedApplyStatus apply(const MarketDataEvent& event);
    // Call before replaying an externally recovered snapshot after a feed gap.
    void reset();

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] std::optional<Price> mid_price() const;
    [[nodiscard]] std::vector<Level> bids(std::size_t depth) const;
    [[nodiscard]] std::vector<Level> asks(std::size_t depth) const;
    [[nodiscard]] SequenceNumber last_sequence() const { return last_sequence_; }
    [[nodiscard]] bool synchronized() const { return synchronized_; }

private:
    struct VisibleOrder {
        Price price{};
        Quantity quantity{};
        Side side{Side::Buy};
        SymbolId symbol{};
    };

    std::unordered_map<OrderId, VisibleOrder> orders_;
    std::map<Price, Quantity, std::greater<Price>> bids_;
    std::map<Price, Quantity, std::less<Price>> asks_;
    SequenceNumber last_sequence_{};
    bool synchronized_{true};

    void add_depth(Side side, Price price, Quantity quantity);
    void remove_depth(Side side, Price price, Quantity quantity);
};
