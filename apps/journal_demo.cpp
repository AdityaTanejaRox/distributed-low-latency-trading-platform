#include "llt/exchange_gateway.hpp"
#include "llt/journal.hpp"
#include "llt/logging.hpp"
#include "llt/oms.hpp"
#include "llt/time.hpp"

#include <cstdio>
#include <string>

using namespace llt;

// ============================================================
// Phase 3 Journal Demo
// ============================================================
//
// This demo proves the OMS durability boundary:
//
//   1. Strategy signal arrives.
//   2. OMS converts signal into NewOrder.
//   3. OMS journals OrderIntent BEFORE sending to gateway.
//   4. Gateway sends Ack.
//   5. OMS journals GatewayAck.
//   6. JournalReader replays the binary journal.
// ============================================================

Envelope make_test_signal() 
{
    Signal signal{};

    signal.header.type = MsgType::Signal;
    signal.header.source_node = 2;
    signal.header.destination_node = 3;
    signal.header.sequence = 1;
    signal.header.send_ts_ns = now_ns();

    signal.symbol_id = 1;
    signal.side = Side::Buy;
    signal.limit_px = 10025;
    signal.qty = 1;
    signal.confidence_bps = 9000;

    Envelope env{};
    env.type = MsgType::Signal;
    env.payload.signal = signal;

    return env;
}

int main() 
{
    start_async_logger("logs/journal_demo.log");

    const std::string journal_path = "journals/oms_journal_demo.bin";

    // Remove old demo journal so the sample output is deterministic.
    std::remove(journal_path.c_str());

    log(LogLevel::Info, "journal_demo", "starting phase 3 journal demo");

    JournalWriter journal{journal_path};
    OrderManager oms{
        3,
        RiskLimits{
            .max_position = 10,
            .max_order_qty = 2,
            .max_notional = 1'000'000
        }
    };

    ExchangeGateway gateway{4};

    Envelope signal = make_test_signal();

    auto maybe_order = oms.on_signal(signal.payload.signal);

    if (!maybe_order) 
    {
        log(LogLevel::Error, "journal_demo", "OMS failed to produce order");
        stop_async_logger();
        return 1;
    }

    if (maybe_order->type != MsgType::NewOrder) 
    {
        log(LogLevel::Warn, "journal_demo", "OMS rejected signal");
        stop_async_logger();
        return 1;
    }

    // Critical durability rule:
    //
    // Persist order intent BEFORE sending to gateway.
    //
    // If we crash after this point, recovery can see that the OMS intended
    // to send this order.
    journal.append(
        JournalRecordType::OrderIntent,
        *maybe_order,
        maybe_order->payload.new_order.header.sequence
    );

    log(LogLevel::Info, "journal_demo", "journaled order intent before gateway send");

    auto maybe_ack =
        gateway.send_order(maybe_order->payload.new_order);

    if (!maybe_ack) 
    {
        log(LogLevel::Error, "journal_demo", "gateway failed to produce response");
        stop_async_logger();
        return 1;
    }

    if (maybe_ack->type == MsgType::Ack) 
    {
        oms.on_gateway_ack(maybe_ack->payload.ack);

        journal.append(
            JournalRecordType::GatewayAck,
            *maybe_ack,
            maybe_ack->payload.ack.header.sequence
        );

        log(LogLevel::Info, "journal_demo", "journaled gateway ack");
    }

    journal.close();

    JournalReader reader{journal_path};
    const auto records = reader.read_all();

    for (const auto& record : records) 
    {
        log(
            LogLevel::Info,
            "journal_demo",
            to_string(record.header.record_type)
        );
    }

    log(LogLevel::Info, "journal_demo", "phase 3 journal demo complete");

    stop_async_logger();
    return 0;
}