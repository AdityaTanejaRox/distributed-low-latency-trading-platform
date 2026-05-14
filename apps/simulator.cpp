#include "llt/exchange_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/message_bus.hpp"
#include "llt/oms.hpp"
#include "llt/strategy.hpp"
#include "llt/time.hpp"

#include <iostream>
#include <thread>

using namespace llt;

int main() 
{
    log(LogLevel::Info, "simulator", "starting full local pipeline simulation");

    LocalBus bus;

    StrategyEngine strategy{2};
    OrderManager oms
    {
        3, RiskLimits
        {
            .max_position = 10,
            .max_order_qty = 2,
            .max_notional = 1'000'000
        }
    };
    ExchangeGateway gateway{4};

    Sequence md_seq = 0;

    for (int i = 0; i < 25; ++i) 
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
        md.bid_px = 10000 + i;
        md.ask_px = 10005 + i;
        md.bid_qty = 100;
        md.ask_qty = 100;
        md.exchange_sequence = md_seq;

        Envelope md_env{};
        md_env.type = MsgType::MarketData;
        md_env.payload.market_data = md;

        bus.market_to_strategy.push(md_env);

        while (auto maybe = bus.market_to_strategy.pop()) 
        {
            auto& env = *maybe;
            auto signal = strategy.on_market_data(env.payload.market_data);

            if (signal) 
            {
                Envelope sig_env{};
                sig_env.type = MsgType::Signal;
                sig_env.payload.signal = *signal;
                bus.strategy_to_oms.push(sig_env);

                log(LogLevel::Info, "strategy", "generated signal");
            }
        }

        while (auto maybe = bus.strategy_to_oms.pop()) 
        {
            auto out = oms.on_signal(maybe->payload.signal);
            if (out) 
            {
                if (out->type == MsgType::NewOrder) 
                {
                    bus.oms_to_gateway.push(*out);
                    log(LogLevel::Info, "oms", "accepted signal and created new order");
                } 
                else 
                {
                    log(LogLevel::Warn, "oms", "rejected signal");
                }
            }
        }

        while (auto maybe = bus.oms_to_gateway.pop()) 
        {
            auto response = gateway.send_order(maybe->payload.new_order);
            if (response) 
            {
                bus.gateway_to_oms.push(*response);
            }
        }

        while (auto maybe = bus.gateway_to_oms.pop()) 
        {
            if (maybe->type == MsgType::Ack) 
            {
                oms.on_gateway_ack(maybe->payload.ack);
                log(LogLevel::Info, "gateway", "order acked by simulated exchange");
            } 
            else if (maybe->type == MsgType::Reject) 
            {
                oms.on_gateway_reject(maybe->payload.reject);
                log(LogLevel::Warn, "gateway", "order rejected by simulated exchange");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    log(LogLevel::Info, "simulator", "simulation complete");
    return 0;
}