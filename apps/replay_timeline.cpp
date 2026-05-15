#include "llt/logging.hpp"
#include "llt/replay.hpp"

#include <iostream>
#include <string>

using namespace llt;

namespace
{
    std::string describe(const ReplayEvent& event)
    {
        const auto type = event.envelope.type;

        if (type == MsgType::MarketData)
        {
            const auto& md = event.envelope.payload.market_data;

            return "MarketData symbol=" + md.symbol.str() +
                   " bid=" + std::to_string(md.bid_px) +
                   " ask=" + std::to_string(md.ask_px);
        }

        if (type == MsgType::Signal)
        {
            const auto& s = event.envelope.payload.signal;

            return "Signal side=" + std::string(to_string(s.side)) +
                   " px=" + std::to_string(s.limit_px) +
                   " qty=" + std::to_string(s.qty);
        }

        if (type == MsgType::NewOrder)
        {
            const auto& o = event.envelope.payload.new_order;

            return "NewOrder clOrdId=" + std::to_string(o.client_order_id) +
                   " side=" + std::string(to_string(o.side)) +
                   " px=" + std::to_string(o.limit_px) +
                   " qty=" + std::to_string(o.qty);
        }

        if (type == MsgType::Ack)
        {
            const auto& ack = event.envelope.payload.ack;

            return "Ack clOrdId=" + std::to_string(ack.client_order_id) +
                   " exchOrderId=" + std::to_string(ack.exchange_order_id);
        }

        if (type == MsgType::Reject)
        {
            const auto& rej = event.envelope.payload.reject;

            return "Reject clOrdId=" + std::to_string(rej.client_order_id);
        }

        return std::string(to_string(type));
    }
}

int main(int argc, char** argv)
{
    start_async_logger("logs/replay_timeline.log");

    std::string path = "journals/replay_merged.bin";

    if (argc >= 2)
    {
        path = argv[1];
    }

    ReplayReader reader{path};

    if (!reader.open())
    {
        std::cerr << "failed to open " << path << '\n';
        stop_async_logger();
        return 1;
    }

    std::uint64_t count = 0;

    while (auto event = reader.next())
    {
        ++count;

        std::cout
            << event->header.timestamp_ns
            << " | "
            << to_string(event->header.node_role)
            << " | seq="
            << event->header.sequence
            << " | "
            << describe(*event)
            << '\n';
    }

    reader.close();

    std::cout << "timeline_events=" << count << '\n';

    stop_async_logger();
    return 0;
}