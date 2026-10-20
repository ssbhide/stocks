#pragma once

#include "../common/order.hpp"
#include "../common/trade.hpp"

#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

enum class BookChangeType { Rested, Reduced, Removed };

struct BookChange {
    BookChangeType type{};
    Order order{};
};

struct MatchResult {
    std::vector<Trade> trades;
    // During matching, changes[0..trades.size()) correspond to the passive
    // order affected by each trade. A possible final Rested change follows.
    std::vector<BookChange> changes;
    Quantity executed{};
    Quantity leaves{};
    bool rested{};
};

struct AmendResult {
    bool found{};
    bool priority_lost{};
    std::optional<Order> before;
    std::optional<Order> after;
};

// Price-time FIFO central limit order book. The price maps provide O(1) best
// price access; per-price lists retain FIFO; the locator map makes cancels O(1).
class OrderBook {
public:
    MatchResult add(Order incoming);
    std::optional<Order> cancel(OrderId id);
    AmendResult amend_quantity(OrderId id, Quantity new_quantity);

    [[nodiscard]] std::optional<Order> find(OrderId id) const;
    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] std::size_t order_count() const { return locations_.size(); }

private:
    using Queue = std::list<Order>;
    struct PriceLevel {
        Price price{};
        Queue orders;
    };
    struct Location {
        Side side{};
        Price price{};
        Queue::iterator iterator;
    };

    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>> asks_;
    std::unordered_map<OrderId, Location> locations_;

    bool crosses(const Order& incoming) const;
    void rest(Order order);
    void erase_location(const Location& location);
};
