#include "local_book.hpp"

void LocalOrderBook::add_depth(Side side, Price price, Quantity quantity) {
    if (side == Side::Buy) bids_[price] += quantity;
    else asks_[price] += quantity;
}

void LocalOrderBook::remove_depth(Side side, Price price, Quantity quantity) {
    if (side == Side::Buy) {
        const auto level = bids_.find(price);
        if (level == bids_.end() || level->second < quantity) return;
        level->second -= quantity;
        if (level->second == 0) bids_.erase(level);
    } else {
        const auto level = asks_.find(price);
        if (level == asks_.end() || level->second < quantity) return;
        level->second -= quantity;
        if (level->second == 0) asks_.erase(level);
    }
}

FeedApplyStatus LocalOrderBook::apply(const MarketDataEvent& event) {
    if (last_sequence_ != 0 && event.sequence != last_sequence_ + 1) {
        synchronized_ = false;
        return FeedApplyStatus::GapDetected;
    }
    last_sequence_ = event.sequence;

    if (event.type == MarketDataType::Trade) return FeedApplyStatus::Applied;
    if (event.type == MarketDataType::Add) {
        if (event.quantity == 0 || orders_.contains(event.order_id)) {
            synchronized_ = false;
            return FeedApplyStatus::InvalidEvent;
        }
        orders_.emplace(event.order_id, VisibleOrder{event.price, event.quantity, event.side, event.symbol});
        add_depth(event.side, event.price, event.quantity);
        return FeedApplyStatus::Applied;
    }

    const auto found = orders_.find(event.order_id);
    if (found == orders_.end()) {
        synchronized_ = false;
        return FeedApplyStatus::InvalidEvent;
    }
    if (event.type == MarketDataType::Cancel) {
        remove_depth(found->second.side, found->second.price, found->second.quantity);
        orders_.erase(found);
        return FeedApplyStatus::Applied;
    }
    if (event.type == MarketDataType::Modify) {
        if (event.quantity == 0) {
            synchronized_ = false;
            return FeedApplyStatus::InvalidEvent;
        }
        remove_depth(found->second.side, found->second.price, found->second.quantity);
        found->second.quantity = event.quantity;
        add_depth(found->second.side, found->second.price, found->second.quantity);
        return FeedApplyStatus::Applied;
    }
    synchronized_ = false;
    return FeedApplyStatus::InvalidEvent;
}

void LocalOrderBook::reset() {
    orders_.clear();
    bids_.clear();
    asks_.clear();
    last_sequence_ = 0;
    synchronized_ = true;
}

std::optional<Price> LocalOrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> LocalOrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> LocalOrderBook::mid_price() const {
    const auto bid = best_bid();
    const auto ask = best_ask();
    if (!bid || !ask) return std::nullopt;
    return *bid + (*ask - *bid) / 2;
}

std::vector<Level> LocalOrderBook::bids(std::size_t depth) const {
    std::vector<Level> result;
    for (auto it = bids_.begin(); it != bids_.end() && result.size() < depth; ++it) {
        result.push_back(Level{it->first, it->second});
    }
    return result;
}

std::vector<Level> LocalOrderBook::asks(std::size_t depth) const {
    std::vector<Level> result;
    for (auto it = asks_.begin(); it != asks_.end() && result.size() < depth; ++it) {
        result.push_back(Level{it->first, it->second});
    }
    return result;
}
