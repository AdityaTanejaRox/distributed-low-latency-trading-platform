#include "llt/strategy.hpp"
#include "llt/time.hpp"

namespace llt 
{
    StrategyEngine::StrategyEngine(NodeId node_id)
        : node_id_(node_id) {}

    std::optional<Signal> StrategyEngine::on_market_data(const MarketDataUpdate& update) 
    {
        book_.apply(update);

        const auto maybe_book = book_.snapshot();
        if (!maybe_book) 
        {
            return std::nullopt; // no data to generate signal
        }

        const auto& tob = *maybe_book;
        const auto spread = tob.ask_px - tob.bid_px;

        if (spread <= 0) 
        {
            return std::nullopt; // invalid market data, no signal
        }

        if (spread >= 4) // generate a buy signal if spread is wide enough
        {
            Signal signal{};
            signal.header.type = MsgType::Signal;
            signal.header.source_node = node_id_;
            signal.header.destination_node = 3;
            signal.header.sequence = ++signal_sequence_;
            signal.header.send_ts_ns = now_ns();
            signal.symbol_id = tob.symbol_id;
            signal.side = Side::Buy;
            signal.limit_px = tob.bid_px + 1;
            signal.qty = 1;
            signal.confidence_bps = 7500;
            return signal;
        }

        return std::nullopt;
    }
}