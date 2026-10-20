#pragma once

#include "../market_data/publisher.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

class UdpMulticastPublisher final : public IMarketDataPublisher {
public:
    UdpMulticastPublisher(std::string group, std::uint16_t port, unsigned char ttl = 1);
    ~UdpMulticastPublisher() override;

    UdpMulticastPublisher(const UdpMulticastPublisher&) = delete;
    UdpMulticastPublisher& operator=(const UdpMulticastPublisher&) = delete;

    void open();
    void publish(const MarketDataEvent& event) override;

private:
    std::string group_;
    std::uint16_t port_;
    unsigned char ttl_;
    int socket_{-1};
};

class UdpMulticastSubscriber {
public:
    UdpMulticastSubscriber(std::string group, std::uint16_t port);
    ~UdpMulticastSubscriber();

    UdpMulticastSubscriber(const UdpMulticastSubscriber&) = delete;
    UdpMulticastSubscriber& operator=(const UdpMulticastSubscriber&) = delete;

    void open();
    [[nodiscard]] std::optional<MarketDataEvent> receive_for(std::chrono::milliseconds timeout);

private:
    std::string group_;
    std::uint16_t port_;
    int socket_{-1};
};
