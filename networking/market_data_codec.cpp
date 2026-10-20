#include "market_data_codec.hpp"

namespace {
void put_u8(std::vector<std::uint8_t>& output, std::uint8_t value) {
    output.push_back(value);
}

void put_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void put_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint8_t get_u8(const std::uint8_t*& input) {
    return *input++;
}

std::uint32_t get_u32(const std::uint8_t*& input) {
    std::uint32_t value = 0;
    for (int shift = 24; shift >= 0; shift -= 8) value |= static_cast<std::uint32_t>(*input++) << shift;
    return value;
}

std::uint64_t get_u64(const std::uint8_t*& input) {
    std::uint64_t value = 0;
    for (int shift = 56; shift >= 0; shift -= 8) value |= static_cast<std::uint64_t>(*input++) << shift;
    return value;
}
} // namespace

std::vector<std::uint8_t> MarketDataCodec::encode(const MarketDataEvent& event) {
    std::vector<std::uint8_t> output;
    output.reserve(packet_size);
    put_u32(output, magic);
    put_u8(output, version);
    put_u8(output, static_cast<std::uint8_t>(event.type));
    put_u8(output, 0);
    put_u8(output, 0);
    put_u64(output, event.sequence);
    put_u64(output, event.timestamp);
    put_u64(output, event.order_id);
    put_u64(output, event.match_order_id);
    put_u64(output, static_cast<std::uint64_t>(event.price));
    put_u32(output, event.quantity);
    put_u32(output, event.symbol);
    put_u8(output, static_cast<std::uint8_t>(event.side));
    put_u8(output, 0);
    put_u8(output, 0);
    put_u8(output, 0);
    return output;
}

std::optional<MarketDataEvent> MarketDataCodec::decode(const std::uint8_t* bytes, std::size_t size) {
    if (size != packet_size) return std::nullopt;
    const std::uint8_t* input = bytes;
    if (get_u32(input) != magic || get_u8(input) != version) return std::nullopt;
    const auto type = static_cast<MarketDataType>(get_u8(input));
    if (static_cast<std::uint8_t>(type) > static_cast<std::uint8_t>(MarketDataType::Trade)) {
        return std::nullopt;
    }
    get_u8(input);
    get_u8(input);

    MarketDataEvent event;
    event.sequence = get_u64(input);
    event.timestamp = get_u64(input);
    event.order_id = get_u64(input);
    event.match_order_id = get_u64(input);
    event.price = static_cast<Price>(get_u64(input));
    event.quantity = get_u32(input);
    event.symbol = get_u32(input);
    event.side = static_cast<Side>(get_u8(input));
    if (event.side != Side::Buy && event.side != Side::Sell) return std::nullopt;
    event.type = type;
    return event;
}
