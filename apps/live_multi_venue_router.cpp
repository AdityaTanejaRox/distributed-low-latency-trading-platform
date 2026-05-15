#include "llt/journal.hpp"
#include "llt/kill_switch.hpp"
#include "llt/live_market_data_connectors.hpp"
#include "llt/logging.hpp"
#include "llt/metrics.hpp"
#include "llt/multi_venue_router.hpp"
#include "llt/replay.hpp"
#include "llt/threading.hpp"
#include "llt/time.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace llt;

namespace
{
    std::atomic<bool> running{true};

    void handle_signal(int)
    {
        running.store(false, std::memory_order_release);
    }

    int env_int(const char* name, int fallback)
    {
        if (const char* v = std::getenv(name))
        {
            return std::atoi(v);
        }

        return fallback;
    }

    Envelope make_market_data_env(const MarketDataUpdate& update)
    {
        Envelope env{};
        env.type = MsgType::MarketData;
        env.payload.market_data = update;
        return env;
    }

    Envelope make_route_signal_env(const RouteDecision& decision, Sequence seq)
    {
        Signal signal{};

        signal.header.type = MsgType::Signal;
        signal.header.source_node = 2;
        signal.header.destination_node = 3;
        signal.header.sequence = seq;
        signal.header.send_ts_ns = now_ns();

        signal.symbol_id = decision.symbol_id;
        signal.side = decision.side;
        signal.limit_px = decision.limit_px;
        signal.qty = decision.qty;
        signal.confidence_bps = 9000;

        Envelope env{};
        env.type = MsgType::Signal;
        env.payload.signal = signal;

        return env;
    }

    struct SharedState
    {
        std::mutex mutex;

        MultiVenueRouter router;

        ReplayWriter replay_writer{
            "journals/live_multi_venue_router_replay.bin",
            ReplayWriteMode::Truncate,
            NodeRole::Simulator
        };

        JournalWriter journal{
            "journals/live_multi_venue_router_journal.bin"
        };

        KillSwitch kill_switch{
            KillSwitchLimits{
                .max_routes_per_run = 1'000'000,
                .max_rejects_per_run = 100,
                .max_gateway_disconnects = 5
            },
            "controls/HALT"
        };

        Sequence replay_seq{0};
        Sequence journal_seq{0};
        Sequence signal_seq{0};
        std::uint64_t updates_seen{0};
        std::uint64_t routes_seen{0};
    };

    void process_update(SharedState& state, const NormalizedMarketData& md)
    {
        const TimestampNs now = now_ns();

        std::lock_guard<std::mutex> lock(state.mutex);

        if (!state.kill_switch.trading_enabled())
        {
            return;
        }

        metric_inc(MetricCounter::MarketDataReceived);

        const auto& update = md.update;

        if (update.header.recv_ts_ns > 0 && now >= update.header.recv_ts_ns)
        {
            metric_latency(now - update.header.recv_ts_ns);
        }

        if (!state.router.update(md))
        {
            metric_inc(MetricCounter::QueueDrops);
            log(LogLevel::Warn, "live_multi_venue_router", "rejected invalid market data update");
            return;
        }

        Envelope md_env = make_market_data_env(update);

        state.replay_writer.append(md_env, ++state.replay_seq, now_ns());
        state.journal.append(JournalRecordType::RuntimeMarker, md_env, ++state.journal_seq);

        ++state.updates_seen;

        Signal buy{};
        buy.symbol_id = update.symbol_id;
        buy.side = Side::Buy;
        buy.qty = 1;

        Signal sell{};
        sell.symbol_id = update.symbol_id;
        sell.side = Side::Sell;
        sell.qty = 1;

        const auto buy_route = state.router.route_signal(buy);
        const auto sell_route = state.router.route_signal(sell);

        if (buy_route.routeable)
        {
            state.kill_switch.on_route();
            metric_inc(MetricCounter::SignalsGenerated);

            Envelope route_env =
                make_route_signal_env(buy_route, ++state.signal_seq);

            state.replay_writer.append(route_env, ++state.replay_seq, now_ns());
            state.journal.append(JournalRecordType::RuntimeMarker, route_env, ++state.journal_seq);

            ++state.routes_seen;

            log(LogLevel::Info, "router_buy", buy_route.reason.c_str());
        }

        if (sell_route.routeable)
        {
            state.kill_switch.on_route();
            metric_inc(MetricCounter::SignalsGenerated);

            Envelope route_env =
                make_route_signal_env(sell_route, ++state.signal_seq);

            state.replay_writer.append(route_env, ++state.replay_seq, now_ns());
            state.journal.append(JournalRecordType::RuntimeMarker, route_env, ++state.journal_seq);

            ++state.routes_seen;

            log(LogLevel::Info, "router_sell", sell_route.reason.c_str());
        }

        if (state.updates_seen % 100 == 0)
        {
            MetricsRegistry::instance().write_jsonl("metrics/live_multi_venue_router.jsonl");

            std::cout
                << "updates=" << state.updates_seen
                << " routes=" << state.routes_seen
                << " books=" << state.router.books().size()
                << '\n';

            for (const auto& book : state.router.books())
            {
                std::cout
                    << "venue=" << to_string(book.venue)
                    << " symbol=" << book.symbol.str()
                    << " bid=" << book.bid_px
                    << " ask=" << book.ask_px
                    << " seq=" << book.exchange_sequence
                    << '\n';
            }
        }
    }

    void run_binance_stream(
        SharedState& state,
        const std::string& symbol,
        SymbolId symbol_id,
        int max_updates
    )
    {
        run_binance_live_book_ticker(
            symbol,
            max_updates,
            [&](const NormalizedMarketData& md)
            {
                process_update(state, md);
            },
            symbol_id
        );
    }

    void run_coinbase_stream(
        SharedState& state,
        const std::string& product,
        SymbolId symbol_id,
        int max_updates
    )
    {
        run_coinbase_live_ticker(
            product,
            max_updates,
            [&](const NormalizedMarketData& md)
            {
                process_update(state, md);
            },
            symbol_id
        );
    }

    void run_hyperliquid_stream(
        SharedState& state,
        const std::string& coin,
        SymbolId symbol_id,
        int max_updates
    )
    {
        run_hyperliquid_live_l2book(
            coin,
            max_updates,
            [&](const NormalizedMarketData& md)
            {
                process_update(state, md);
            },
            symbol_id
        );
    }
}

int main()
{
    start_async_logger("logs/live_multi_venue_router.log");
    pin_current_thread_to_cpu(0, "multi-router");

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const int max_updates_per_stream =
        env_int("LLT_MAX_UPDATES_PER_STREAM", 0);

    log(LogLevel::Info, "live_multi_venue_router", "starting live multi-symbol multi-venue router");

    SharedState state;
    state.replay_writer.open();
    state.journal.open();

    std::vector<std::thread> threads;

    threads.emplace_back(run_binance_stream, std::ref(state), "btcusdt", 1, max_updates_per_stream);
    threads.emplace_back(run_binance_stream, std::ref(state), "ethusdt", 2, max_updates_per_stream);
    threads.emplace_back(run_binance_stream, std::ref(state), "solusdt", 3, max_updates_per_stream);

    threads.emplace_back(run_coinbase_stream, std::ref(state), "BTC-USD", 1, max_updates_per_stream);
    threads.emplace_back(run_coinbase_stream, std::ref(state), "ETH-USD", 2, max_updates_per_stream);
    threads.emplace_back(run_coinbase_stream, std::ref(state), "SOL-USD", 3, max_updates_per_stream);

    threads.emplace_back(run_hyperliquid_stream, std::ref(state), "BTC", 1, max_updates_per_stream);
    threads.emplace_back(run_hyperliquid_stream, std::ref(state), "ETH", 2, max_updates_per_stream);
    threads.emplace_back(run_hyperliquid_stream, std::ref(state), "SOL", 3, max_updates_per_stream);

    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    MetricsRegistry::instance().write_jsonl("metrics/live_multi_venue_router.jsonl");

    state.journal.close();
    state.replay_writer.close();

    log(LogLevel::Info, "live_multi_venue_router", "stopped live multi-venue router");
    stop_async_logger();

    return 0;
}