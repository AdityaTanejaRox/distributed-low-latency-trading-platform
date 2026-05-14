#include "llt/exchange_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/message_bus.hpp"
#include "llt/oms.hpp"
#include "llt/strategy.hpp"
#include "llt/threading.hpp"
#include "llt/time.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

using namespace llt;

// ============================================================
// Phase 1 Runtime Topology
// ============================================================
//
// This runtime keeps all components inside one Linux process,
// but gives each component its own dedicated thread.
//
// Thread layout:
//
//   CPU 0 -> market data thread
//   CPU 1 -> strategy thread
//   CPU 2 -> OMS thread
//   CPU 3 -> gateway thread
//
// This gives us the first real low-latency runtime shape:
//
//   market data -> strategy -> OMS -> gateway -> OMS
//
// We are not using TCP yet.
// TCP transport comes later.
// First we make the local threaded pipeline deterministic,
// bounded, and CPU-pinned.
//
// ============================================================

static constexpr int MARKET_DATA_CPU = 0;
static constexpr int STRATEGY_CPU = 1;
static constexpr int OMS_CPU = 2;
static constexpr int GATEWAY_CPU = 3;

// Global shutdown flag.
// In production this would usually be controlled by signal handling,
// an admin command, a kill switch, or a process supervisor.
static std::atomic<bool> g_running{true};

void handle_signal(int) 
{
    g_running.store(false, std::memory_order_release);
}

void market_data_thread(LocalBus& bus) 
{
    pin_current_thread_to_cpu(MARKET_DATA_CPU, "llt-md");

    Sequence md_seq = 0;

    while (g_running.load(std::memory_order_acquire)) 
    {
        MarketDataUpdate md{};

        md.header.type = MsgType::MarketData;
        md.header.source_node = 1;
        md.header.destination_node = 2;
        md.header.sequence = ++md_seq;
        md.header.send_ts_ns = now_ns();
        md.header.recv_ts_ns = now_ns();

        md.symbol_id = 1;
        md.symbol = Symbol{"AAPL"};

        // Prices are represented as integer ticks/cents.
        // This avoids floating point on the hot path.
        md.bid_px = 10000 + static_cast<Price>(md_seq % 25);
        md.ask_px = md.bid_px + 5;
        md.bid_qty = 100;
        md.ask_qty = 100;
        md.exchange_sequence = md_seq;

        Envelope env{};
        env.type = MsgType::MarketData;
        env.payload.market_data = md;

        // Bounded push.
        // If strategy falls behind, we drop instead of allowing unbounded
        // queue growth and hidden latency.
        if (!bus.market_to_strategy.push(env)) 
        {
            log(LogLevel::Warn, "market_data", "market_to_strategy queue full; dropped market data");
        }

        // Simulate a market data rate.
        // Later phases can replace this with real feed IO or replay.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    log(LogLevel::Info, "market_data", "thread stopped");
}

void strategy_thread(LocalBus& bus) 
{
    pin_current_thread_to_cpu(STRATEGY_CPU, "llt-strategy");

    StrategyEngine strategy{2};

    while (g_running.load(std::memory_order_acquire)) 
    {
        auto maybe_msg = bus.market_to_strategy.pop();

        if (!maybe_msg) 
        {
            // For now we yield when idle.
            // In a stricter HFT runtime this may become:
            //   - busy spin
            //   - _mm_pause()
            //   - adaptive spin/yield
            // depending on latency vs CPU budget.
            std::this_thread::yield();
            continue;
        }

        Envelope& env = *maybe_msg;

        if (env.type != MsgType::MarketData) 
        {
            continue;
        }

        auto maybe_signal = strategy.on_market_data(env.payload.market_data);

        if (!maybe_signal) 
        {
            continue;
        }

        Envelope out{};
        out.type = MsgType::Signal;
        out.payload.signal = *maybe_signal;

        if (!bus.strategy_to_oms.push(out)) 
        {
            log(LogLevel::Warn, "strategy", "strategy_to_oms queue full; dropped signal");
        } 
        else 
        {
            log(LogLevel::Info, "strategy", "generated signal");
        }
    }

    log(LogLevel::Info, "strategy", "thread stopped");
}

void oms_thread(LocalBus& bus) 
{
    pin_current_thread_to_cpu(OMS_CPU, "llt-oms");

    OrderManager oms{
        3,
        RiskLimits{
            .max_position = 10,
            .max_order_qty = 2,
            .max_notional = 1'000'000
        }
    };

    while (g_running.load(std::memory_order_acquire)) 
    {
        bool did_work = false;

        // Consume strategy signals.
        if (auto maybe_signal = bus.strategy_to_oms.pop()) 
        {
            did_work = true;

            if (maybe_signal->type == MsgType::Signal) 
            {
                auto maybe_order = oms.on_signal(maybe_signal->payload.signal);

                if (maybe_order) 
                {
                    if (maybe_order->type == MsgType::NewOrder) {
                        if (!bus.oms_to_gateway.push(*maybe_order)) 
                        {
                            log(LogLevel::Warn, "oms", "oms_to_gateway queue full; dropped order");
                        } 
                        else 
                        {
                            log(LogLevel::Info, "oms", "accepted signal and created order");
                        }
                    } 
                    else if (maybe_order->type == MsgType::Reject) 
                    {
                        log(LogLevel::Warn, "oms", "signal rejected by risk engine");
                    }
                }
            }
        }

        // Consume gateway responses.
        if (auto maybe_gateway_msg = bus.gateway_to_oms.pop()) 
        {
            did_work = true;

            if (maybe_gateway_msg->type == MsgType::Ack) 
            {
                oms.on_gateway_ack(maybe_gateway_msg->payload.ack);
                log(LogLevel::Info, "oms", "processed gateway ack");
            } 
            else if (maybe_gateway_msg->type == MsgType::Reject) 
            {
                oms.on_gateway_reject(maybe_gateway_msg->payload.reject);
                log(LogLevel::Warn, "oms", "processed gateway reject");
            }
        }

        if (!did_work) 
        {
            std::this_thread::yield();
        }
    }

    log(LogLevel::Info, "oms", "thread stopped");
}

void gateway_thread(LocalBus& bus) 
{
    pin_current_thread_to_cpu(GATEWAY_CPU, "llt-gateway");

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

        auto maybe_response = gateway.send_order(maybe_order->payload.new_order);

        if (!maybe_response) 
        {
            continue;
        }

        if (!bus.gateway_to_oms.push(*maybe_response)) 
        {
            log(LogLevel::Warn, "gateway", "gateway_to_oms queue full; dropped gateway response");
        } 
        else 
        {
            log(LogLevel::Info, "gateway", "sent order and produced exchange response");
        }
    }

    log(LogLevel::Info, "gateway", "thread stopped");
}

int main() 
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    start_async_logger("logs/threaded_runtime.log");

    log(LogLevel::Info, "threaded_runtime", "starting phase 1 CPU-pinned multi-threaded runtime");

    LocalBus bus;

    std::thread md{market_data_thread, std::ref(bus)};
    std::thread strategy{strategy_thread, std::ref(bus)};
    std::thread oms{oms_thread, std::ref(bus)};
    std::thread gateway{gateway_thread, std::ref(bus)};

    // Run for a bounded demo window.
    // Ctrl+C also shuts this down.
    std::this_thread::sleep_for(std::chrono::seconds(30));
    g_running.store(false, std::memory_order_release);

    md.join();
    strategy.join();
    oms.join();
    gateway.join();

    log(LogLevel::Info, "threaded_runtime", "shutdown complete");

    stop_async_logger();
    
    return 0;
}