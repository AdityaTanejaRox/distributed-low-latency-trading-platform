#include "llt/exchange_gateway.hpp"
#include "llt/live_market_data_connectors.hpp"
#include "llt/logging.hpp"
#include "llt/message_bus.hpp"
#include "llt/oms.hpp"
#include "llt/order_book.hpp"
#include "llt/strategy.hpp"
#include "llt/threading.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

using namespace llt;

// ============================================================
// Phase 4B Live Market Data Runtime
// ============================================================
//
// This binary runs the actual platform pipeline using live market data:
//
//   Live Venue WebSocket
//        ->
//   Venue Normalizer
//        ->
//   Internal TopOfBook
//        ->
//   MarketDataUpdate queue
//        ->
//   StrategyEngine
//        ->
//   OMS
//        ->
//   Simulated Gateway
//
// Phase 5 will replace the simulated gateway with testnet order submission.
// ============================================================

static std::atomic<bool> g_running{true};

void handle_signal(int) 
{
    g_running.store(false, std::memory_order_release);
}

void strategy_thread(LocalBus& bus) 
{
    pin_current_thread_to_cpu(1, "live-strategy");

    StrategyEngine strategy{2};

    while (g_running.load(std::memory_order_acquire)) 
    {
        auto maybe_msg = bus.market_to_strategy.pop();

        if (!maybe_msg) {
            std::this_thread::yield();
            continue;
        }

        if (maybe_msg->type != MsgType::MarketData) 
        {
            continue;
        }

        auto maybe_signal = strategy.on_market_data(maybe_msg->payload.market_data);

        if (!maybe_signal) 
        {
            continue;
        }

        Envelope out{};
        out.type = MsgType::Signal;
        out.payload.signal = *maybe_signal;

        if (bus.strategy_to_oms.push(out)) 
        {
            log(LogLevel::Info, "live_strategy", "generated signal from live market data");
        }
    }
}

void oms_thread(LocalBus& bus) 
{
    pin_current_thread_to_cpu(2, "live-oms");

    OrderManager oms{
        3,
        RiskLimits{
            .max_position = 10,
            .max_order_qty = 2,
            .max_notional = 10'000'000
        }
    };

    while (g_running.load(std::memory_order_acquire)) 
    {
        bool did_work = false;

        if (auto maybe_signal = bus.strategy_to_oms.pop()) 
        {
            did_work = true;

            auto maybe_order = oms.on_signal(maybe_signal->payload.signal);

            if (maybe_order && maybe_order->type == MsgType::NewOrder) 
            {
                bus.oms_to_gateway.push(*maybe_order);
                log(LogLevel::Info, "live_oms", "created risk-checked order");
            }
        }

        if (auto maybe_gateway = bus.gateway_to_oms.pop()) 
        {
            did_work = true;

            if (maybe_gateway->type == MsgType::Ack) 
            {
                oms.on_gateway_ack(maybe_gateway->payload.ack);
                log(LogLevel::Info, "live_oms", "processed simulated gateway ack");
            } 
            else if (maybe_gateway->type == MsgType::Reject) 
            {
                oms.on_gateway_reject(maybe_gateway->payload.reject);
                log(LogLevel::Warn, "live_oms", "processed simulated gateway reject");
            }
        }

        if (!did_work) 
        {
            std::this_thread::yield();
        }
    }
}

void gateway_thread(LocalBus& bus) 
{
    pin_current_thread_to_cpu(3, "live-gateway");

    ExchangeGateway gateway{4};

    while (g_running.load(std::memory_order_acquire)) 
    {
        auto maybe_order = bus.oms_to_gateway.pop();

        if (!maybe_order) 
        {
            std::this_thread::yield();
            continue;
        }

        if (maybe_order->type != MsgType::NewOrder) 
        {
            continue;
        }

        auto response = gateway.send_order(maybe_order->payload.new_order);

        if (response) 
        {
            bus.gateway_to_oms.push(*response);
            log(LogLevel::Info, "live_gateway", "simulated gateway accepted order");
        }
    }
}

void market_data_thread(LocalBus& bus, std::string venue) 
{
    pin_current_thread_to_cpu(0, "live-md");

    TopOfBook book;

    auto on_market_data = [&](const NormalizedMarketData& md) {
        const auto& u = md.update;

        book.apply(u);

        auto snapshot = book.snapshot();

        if (snapshot) 
        {
            std::cout
                << "book venue=" << to_string(md.venue)
                << " symbol=" << snapshot->symbol.str()
                << " seq=" << snapshot->exchange_sequence
                << " bid=" << snapshot->bid_px
                << " bid_qty=" << snapshot->bid_qty
                << " ask=" << snapshot->ask_px
                << " ask_qty=" << snapshot->ask_qty
                << '\n';
        }

        Envelope env{};
        env.type = MsgType::MarketData;
        env.payload.market_data = u;

        if (!bus.market_to_strategy.push(env)) 
        {
            log(LogLevel::Warn, "live_md", "market_to_strategy queue full; dropped live update");
        }
    };

    if (venue == "binance") 
    {
        run_binance_live_book_ticker("btcusdt", 0, on_market_data);
    } 
    else if (venue == "coinbase") 
    {
        run_coinbase_live_ticker("BTC-USD", 0, on_market_data);
    } 
    else if (venue == "hyperliquid") 
    {
        run_hyperliquid_live_l2book("BTC", 0, on_market_data);
    } 
    else 
    {
        log(LogLevel::Error, "live_md", "unknown venue");
    }

    g_running.store(false, std::memory_order_release);
}

int main(int argc, char** argv) 
{
    start_async_logger("logs/live_market_data_runtime.log");

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::string venue = "coinbase";

    if (argc >= 2) 
    {
        venue = argv[1];
    }

    log(LogLevel::Info, "live_runtime", "starting live market data runtime");

    LocalBus bus;

    std::thread md{market_data_thread, std::ref(bus), venue};
    std::thread strategy{strategy_thread, std::ref(bus)};
    std::thread oms{oms_thread, std::ref(bus)};
    std::thread gateway{gateway_thread, std::ref(bus)};

    md.join();

    g_running.store(false, std::memory_order_release);

    strategy.join();
    oms.join();
    gateway.join();

    log(LogLevel::Info, "live_runtime", "shutdown complete");

    stop_async_logger();
    return 0;
}