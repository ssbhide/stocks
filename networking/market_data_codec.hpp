#pragma once

#include "../common/market_data.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Fixed-width, network-byte-order UDP payload. It intentionally avoids sending
// C++ structs directly: padding, endian order, and ABI then cannot corrupt a
// feed when publisher and subscriber differ.
class MarketDataCodec {
public:
    static constexpr std::uint32_t magic = 0x48465431; // "HFT1"
    static constexpr std::uint8_t version = 1;
    static constexpr std::size_t packet_size = 60;

    static std::vector<std::uint8_t> encode(const MarketDataEvent& event);
    static std::optional<MarketDataEvent> decode(const std::uint8_t* bytes, std::size_t size);
};
