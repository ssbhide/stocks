#include "udp_multicast.hpp"

#include "market_data_codec.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
sockaddr_in destination(const std::string& group, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, group.c_str(), &address.sin_addr) != 1) {
        throw std::invalid_argument("multicast group must be an IPv4 address");
    }
    return address;
}

void close_socket(int& socket) {
    if (socket != -1) {
        ::close(socket);
        socket = -1;
    }
}
} // namespace

UdpMulticastPublisher::UdpMulticastPublisher(std::string group, std::uint16_t port, unsigned char ttl)
    : group_(std::move(group)), port_(port), ttl_(ttl) {}

UdpMulticastPublisher::~UdpMulticastPublisher() {
    close_socket(socket_);
}

void UdpMulticastPublisher::open() {
    if (socket_ != -1) return;
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == -1) throw std::runtime_error("could not create multicast send socket");
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl_, sizeof(ttl_)) != 0) {
        close_socket(socket_);
        throw std::runtime_error("could not set multicast TTL");
    }
    const unsigned char loopback = 1;
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) != 0) {
        close_socket(socket_);
        throw std::runtime_error("could not enable multicast loopback");
    }
}

void UdpMulticastPublisher::publish(const MarketDataEvent& event) {
    open();
    const auto bytes = MarketDataCodec::encode(event);
    const auto address = destination(group_, port_);
    const auto sent = sendto(socket_, bytes.data(), bytes.size(), 0,
                             reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (sent != static_cast<ssize_t>(bytes.size())) {
        throw std::runtime_error("could not publish complete multicast packet");
    }
}

UdpMulticastSubscriber::UdpMulticastSubscriber(std::string group, std::uint16_t port)
    : group_(std::move(group)), port_(port) {}

UdpMulticastSubscriber::~UdpMulticastSubscriber() {
    close_socket(socket_);
}

void UdpMulticastSubscriber::open() {
    if (socket_ != -1) return;
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == -1) throw std::runtime_error("could not create multicast receive socket");

    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        close_socket(socket_);
        throw std::runtime_error("could not enable address reuse");
    }
    sockaddr_in listen_address{};
    listen_address.sin_family = AF_INET;
    listen_address.sin_port = htons(port_);
    listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_, reinterpret_cast<const sockaddr*>(&listen_address), sizeof(listen_address)) != 0) {
        close_socket(socket_);
        throw std::runtime_error("could not bind multicast receive socket");
    }

    ip_mreq membership{};
    if (inet_pton(AF_INET, group_.c_str(), &membership.imr_multiaddr) != 1) {
        close_socket(socket_);
        throw std::invalid_argument("multicast group must be an IPv4 address");
    }
    membership.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
        close_socket(socket_);
        throw std::runtime_error("could not join multicast group");
    }
}

std::optional<MarketDataEvent> UdpMulticastSubscriber::receive_for(
    std::chrono::milliseconds timeout) {
    open();
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket_, &read_set);
    timeval duration{};
    duration.tv_sec = static_cast<decltype(duration.tv_sec)>(timeout.count() / 1000);
    duration.tv_usec = static_cast<decltype(duration.tv_usec)>((timeout.count() % 1000) * 1000);
    const int ready = select(socket_ + 1, &read_set, nullptr, nullptr, &duration);
    if (ready == 0) return std::nullopt;
    if (ready < 0) throw std::runtime_error("multicast select failed");

    std::array<std::uint8_t, 256> buffer{};
    const auto received = recvfrom(socket_, buffer.data(), buffer.size(), 0, nullptr, nullptr);
    if (received < 0) throw std::runtime_error("multicast receive failed");
    return MarketDataCodec::decode(buffer.data(), static_cast<std::size_t>(received));
}
