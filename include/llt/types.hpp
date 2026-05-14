#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace llt {

using SymbolId = std::uint32_t;
using OrderId = std::uint64_t;
using ClientOrderId = std::uint64_t;
using Sequence = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::int64_t;
using NodeId = std::uint32_t;
using TimestampNs = std::uint64_t;

constexpr std::size_t SYMBOL_LEN = 16;

enum class Side : std::uint8_t 
{
    Buy = 1,
    Sell = 2
};

enum class MsgType : std::uint8_t 
{
    Unknown = 0,
    MarketData = 1,
    Signal = 2,
    NewOrder = 3,
    CancelOrder = 4,
    Ack = 5,
    Fill = 6,
    Reject = 7,
    Heartbeat = 8,
    RiskState = 9
};

enum class NodeRole : std::uint8_t 
{
    MarketData = 1,
    Strategy = 2,
    OMS = 3,
    Gateway = 4,
    Simulator = 5
};

enum class OrderState : std::uint8_t 
{
    New = 1,
    Accepted = 2,
    Rejected = 3,
    Live = 4,
    Filled = 5,
    Cancelled = 6
};

enum class RejectReason : std::uint8_t 
{
    None = 0,
    RiskLimit = 1,
    DuplicateOrder = 2,
    StaleMarketData = 3,
    GatewayUnavailable = 4,
    NetworkPartition = 5
};

struct Symbol 
{
    std::array<char, SYMBOL_LEN> value{};

    Symbol() = default;

    explicit Symbol(std::string_view s) 
    {
        const std::size_t n = std::min(s.size(), SYMBOL_LEN - 1);
        std::memcpy(value.data(), s.data(), n);
        value[n] = '\0';
    }

    std::string str() const 
    {
        return std::string(value.data());
    }
};

struct MessageHeader 
{
    MsgType type{MsgType::Unknown};
    NodeId source_node{0};
    NodeId destination_node{0};
    Sequence sequence{0};
    TimestampNs send_ts_ns{0};
    TimestampNs recv_ts_ns{0};
};

struct MarketDataUpdate 
{
    MessageHeader header{};
    SymbolId symbol_id{0};
    Symbol symbol{};
    Price bid_px{0};
    Quantity bid_qty{0};
    Price ask_px{0};
    Quantity ask_qty{0};
    Sequence exchange_sequence{0};
};

struct Signal 
{
    MessageHeader header{};
    SymbolId symbol_id{0};
    Side side{Side::Buy};
    Price limit_px{0};
    Quantity qty{0};
    std::int32_t confidence_bps{0};
};

struct NewOrder 
{
    MessageHeader header{};
    ClientOrderId client_order_id{0};
    SymbolId symbol_id{0};
    Side side{Side::Buy};
    Price limit_px{0};
    Quantity qty{0};
};

struct Ack 
{
    MessageHeader header{};
    ClientOrderId client_order_id{0};
    OrderId exchange_order_id{0};
    OrderState state{OrderState::Accepted};
};

struct Reject 
{
    MessageHeader header{};
    ClientOrderId client_order_id{0};
    RejectReason reason{RejectReason::None};
};

struct Fill 
{
    MessageHeader header{};
    ClientOrderId client_order_id{0};
    OrderId exchange_order_id{0};
    Price fill_px{0};
    Quantity fill_qty{0};
};

struct Heartbeat 
{
    MessageHeader header{};
    NodeRole role{NodeRole::Simulator};
    TimestampNs last_seen_ns{0};
};

struct RiskState 
{
    MessageHeader header{};
    Quantity current_position{0};
    Quantity max_position{0};
    bool trading_enabled{true};
};

struct Envelope 
{
    MsgType type{MsgType::Unknown};

    union Payload 
    {
        MarketDataUpdate market_data;
        Signal signal;
        NewOrder new_order;
        Ack ack;
        Reject reject;
        Fill fill;
        Heartbeat heartbeat;
        RiskState risk_state;

        Payload() {}
        ~Payload() {}
    } payload;

    Envelope() = default;
};

inline const char* to_string(Side side) 
{
    return side == Side::Buy ? "BUY" : "SELL";
}

inline const char* to_string(MsgType type) 
{
    switch (type) 
    {
        case MsgType::MarketData: return "MarketData";
        case MsgType::Signal: return "Signal";
        case MsgType::NewOrder: return "NewOrder";
        case MsgType::CancelOrder: return "CancelOrder";
        case MsgType::Ack: return "Ack";
        case MsgType::Fill: return "Fill";
        case MsgType::Reject: return "Reject";
        case MsgType::Heartbeat: return "Heartbeat";
        case MsgType::RiskState: return "RiskState";
        default: return "Unknown";
    }
}

} 