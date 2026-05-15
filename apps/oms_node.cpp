#include "llt/journal.hpp"
#include "llt/logging.hpp"
#include "llt/metrics.hpp"
#include "llt/oms.hpp"
#include "llt/replay.hpp"
#include "llt/tcp_transport.hpp"
#include "llt/threading.hpp"
#include "llt/time.hpp"

#include <chrono>
#include <thread>

using namespace llt;

static TcpConnection connect_retry(const std::string& host, std::uint16_t port)
{
    while (true) {
        auto conn = TcpClient::connect_to(host, port);

        if (conn) {
            return std::move(*conn);
        }

        log(LogLevel::Warn, "oms_node", "gateway unavailable; retrying");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main()
{
    start_async_logger("logs/oms_node.log");
    pin_current_thread_to_cpu(2, "oms-node");

    log(LogLevel::Info, "oms_node", "starting distributed OMS node on port 21002");

    TcpConnection gateway_conn = connect_retry("gateway-node", 21003);

    TcpServer server;

    if (!server.listen_on(21002)) {
        log(LogLevel::Error, "oms_node", "failed to listen for strategy");
        MetricsRegistry::instance().write_jsonl("metrics/oms_node.jsonl");
        stop_async_logger();
        return 1;
    }

    auto maybe_strategy = server.accept_one();

    if (!maybe_strategy) {
        log(LogLevel::Error, "oms_node", "failed to accept strategy connection");
        MetricsRegistry::instance().write_jsonl("metrics/oms_node.jsonl");
        stop_async_logger();
        return 1;
    }

    TcpConnection strategy_conn = std::move(*maybe_strategy);

    OrderManager oms{
        3,
        RiskLimits{
            .max_position = 10,
            .max_order_qty = 2,
            .max_notional = 100'000'000
        }
    };

    JournalWriter journal{"journals/distributed_oms.bin"};
    journal.open();

    Sequence journal_seq = 0;
    Sequence gateway_seq = 0;

    ReplayWriter replay_writer{
        "journals/replay_oms.bin",
        ReplayWriteMode::Truncate,
        NodeRole::OMS
    };

    replay_writer.open();

    Sequence replay_seq = 0;

    while (true) {
        auto maybe_msg = strategy_conn.recv_envelope();

        if (!maybe_msg) {
            log(LogLevel::Warn, "oms_node", "strategy disconnected");
            break;
        }

        if (maybe_msg->type != MsgType::Signal) {
            continue;
        }

        auto maybe_order = oms.on_signal(maybe_msg->payload.signal);

        if (!maybe_order) {
            continue;
        }

        if (maybe_order->type == MsgType::Reject) {
            metric_inc(MetricCounter::OrdersRejected);
            log(LogLevel::Warn, "oms_node", "signal rejected by OMS risk");
            continue;
        }

        journal.append(JournalRecordType::OrderIntent, *maybe_order, ++journal_seq);
        metric_inc(MetricCounter::JournalWrites);

        replay_writer.append(*maybe_order, ++replay_seq, now_ns());
        metric_inc(MetricCounter::ReplayEventsWritten);

        if (!gateway_conn.send_envelope(*maybe_order, ++gateway_seq)) {
            metric_inc(MetricCounter::GatewayDisconnects);
            metric_inc(MetricCounter::QueueDrops);
            log(LogLevel::Error, "oms_node", "failed to send order to gateway");
            break;
        }

        metric_inc(MetricCounter::OrdersSent);

        auto maybe_response = gateway_conn.recv_envelope();

        if (!maybe_response) {
            metric_inc(MetricCounter::GatewayDisconnects);
            log(LogLevel::Error, "oms_node", "gateway disconnected before response");
            break;
        }

        replay_writer.append(*maybe_response, ++replay_seq, now_ns());
        metric_inc(MetricCounter::ReplayEventsWritten);

        if (maybe_response->type == MsgType::Ack) {
            oms.on_gateway_ack(maybe_response->payload.ack);

            journal.append(JournalRecordType::GatewayAck, *maybe_response, ++journal_seq);
            metric_inc(MetricCounter::JournalWrites);
            metric_inc(MetricCounter::AcksReceived);

            log(LogLevel::Info, "oms_node", "journaled and processed gateway Ack");
        } else if (maybe_response->type == MsgType::Reject) {
            oms.on_gateway_reject(maybe_response->payload.reject);

            journal.append(JournalRecordType::GatewayReject, *maybe_response, ++journal_seq);
            metric_inc(MetricCounter::JournalWrites);
            metric_inc(MetricCounter::OrdersRejected);

            log(LogLevel::Warn, "oms_node", "journaled and processed gateway Reject");
        } else if (maybe_response->type == MsgType::Fill) {
            metric_inc(MetricCounter::FillsReceived);
        }
    }

    MetricsRegistry::instance().write_jsonl("metrics/oms_node.jsonl");
    replay_writer.close();
    journal.close();

    stop_async_logger();
    return 0;
}