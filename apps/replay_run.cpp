#include "llt/exchange_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/oms.hpp"
#include "llt/replay.hpp"
#include "llt/strategy.hpp"

#include <iostream>
#include <string>

using namespace llt;

int main(int argc, char** argv)
{
    start_async_logger("logs/replay_run.log");

    std::string path = "journals/market_data_replay.bin";

    if (argc >= 2)
    {
        path = argv[1];
    }

    log(LogLevel::Info, "replay_run", "starting deterministic replay run");

    ReplayReader reader{path};

    if (!reader.open())
    {
        std::cerr << "failed to open replay file: " << path << '\n';
        stop_async_logger();
        return 1;
    }

    StrategyEngine strategy{2};

    OrderManager oms{
        3,
        RiskLimits{
            .max_position = 10,
            .max_order_qty = 2,
            .max_notional = 100'000'000
        }
    };

    ExchangeGateway gateway{4};

    ReplayStats stats{};

    while (auto event = reader.next())
    {
        ++stats.events_read;

        if (event->envelope.type != MsgType::MarketData)
        {
            continue;
        }

        ++stats.market_data_events;

        auto maybe_signal =
            strategy.on_market_data(event->envelope.payload.market_data);

        if (!maybe_signal)
        {
            continue;
        }

        ++stats.signals_generated;

        auto maybe_order =
            oms.on_signal(*maybe_signal);

        if (!maybe_order)
        {
            continue;
        }

        if (maybe_order->type == MsgType::Reject)
        {
            ++stats.rejects_generated;
            continue;
        }

        if (maybe_order->type != MsgType::NewOrder)
        {
            continue;
        }

        ++stats.orders_generated;

        auto maybe_response =
            gateway.send_order(maybe_order->payload.new_order);

        if (!maybe_response)
        {
            continue;
        }

        if (maybe_response->type == MsgType::Ack)
        {
            oms.on_gateway_ack(maybe_response->payload.ack);
            ++stats.acks_generated;
        }
        else if (maybe_response->type == MsgType::Reject)
        {
            oms.on_gateway_reject(maybe_response->payload.reject);
            ++stats.rejects_generated;
        }
    }

    reader.close();

    std::cout << "Replay complete\n";
    std::cout << "events_read=" << stats.events_read << '\n';
    std::cout << "market_data_events=" << stats.market_data_events << '\n';
    std::cout << "signals_generated=" << stats.signals_generated << '\n';
    std::cout << "orders_generated=" << stats.orders_generated << '\n';
    std::cout << "acks_generated=" << stats.acks_generated << '\n';
    std::cout << "rejects_generated=" << stats.rejects_generated << '\n';

    log(LogLevel::Info, "replay_run", "deterministic replay run complete");

    stop_async_logger();
    return 0;
}