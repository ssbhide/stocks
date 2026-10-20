#pragma once

#include "../common/market_data.hpp"

#include <functional>
#include <mutex>
#include <vector>

class IMarketDataPublisher {
public:
    virtual ~IMarketDataPublisher() = default;
    virtual void publish(const MarketDataEvent& event) = 0;
};

// Useful for deterministic simulations and tests. Production-style publishers
// (such as UdpMulticastPublisher) share the same exchange-facing interface.
class RecordingPublisher final : public IMarketDataPublisher {
public:
    void publish(const MarketDataEvent& event) override {
        std::lock_guard lock(mutex_);
        events_.push_back(event);
    }

    [[nodiscard]] std::vector<MarketDataEvent> snapshot() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

    [[nodiscard]] std::vector<MarketDataEvent> drain() {
        std::lock_guard lock(mutex_);
        auto result = std::move(events_);
        events_.clear();
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::vector<MarketDataEvent> events_;
};

class FanoutPublisher final : public IMarketDataPublisher {
public:
    explicit FanoutPublisher(std::vector<std::reference_wrapper<IMarketDataPublisher>> publishers)
        : publishers_(std::move(publishers)) {}

    void publish(const MarketDataEvent& event) override {
        for (auto& publisher : publishers_) publisher.get().publish(event);
    }

private:
    std::vector<std::reference_wrapper<IMarketDataPublisher>> publishers_;
};
