#include "llt/exchange_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/metrics.hpp"
#include "llt/replay.hpp"
#include "llt/tcp_transport.hpp"
#include "llt/threading.hpp"
#include "llt/time.hpp"

using namespace llt;

int main()
{
    start_async_logger("logs/gateway_node.log");
    pin_current_thread_to_cpu(3, "gateway-node");

    log(LogLevel::Info, "gateway_node", "starting distributed gateway node on port 21003");

    TcpServer server;

    if (!server.listen_on(21003)) {
        log(LogLevel::Error, "gateway_node", "failed to listen");
        MetricsRegistry::instance().write_jsonl("metrics/gateway_node.jsonl");
        stop_async_logger();
        return 1;
    }

    auto maybe_conn = server.accept_one();

    if (!maybe_conn) {
        log(LogLevel::Error, "gateway_node", "failed to accept OMS connection");
        MetricsRegistry::instance().write_jsonl("metrics/gateway_node.jsonl");
        stop_async_logger();
        return 1;
    }

    TcpConnection conn = std::move(*maybe_conn);
    ExchangeGateway gateway{4};
    Sequence response_seq = 0;

    ReplayWriter replay_writer{
        "journals/replay_gateway.bin",
        ReplayWriteMode::Truncate,
        NodeRole::Gateway
    };

    replay_writer.open();

    while (true) {
        auto maybe_msg = conn.recv_envelope();

        if (!maybe_msg) {
            metric_inc(MetricCounter::GatewayDisconnects);
            log(LogLevel::Warn, "gateway_node", "OMS disconnected");
            break;
        }

        if (maybe_msg->type != MsgType::NewOrder) {
            continue;
        }

        metric_inc(MetricCounter::OrdersSent);

        auto response = gateway.send_order(maybe_msg->payload.new_order);

        if (!response) {
            metric_inc(MetricCounter::OrdersRejected);
            log(LogLevel::Error, "gateway_node", "gateway failed to produce response");
            continue;
        }

        replay_writer.append(*response, ++response_seq, now_ns());
        metric_inc(MetricCounter::ReplayEventsWritten);

        if (response->type == MsgType::Ack) {
            metric_inc(MetricCounter::AcksReceived);
        } else if (response->type == MsgType::Reject) {
            metric_inc(MetricCounter::OrdersRejected);
        } else if (response->type == MsgType::Fill) {
            metric_inc(MetricCounter::FillsReceived);
        }

        if (conn.send_envelope(*response, response_seq)) {
            log(LogLevel::Info, "gateway_node", "processed NewOrder and returned Ack/Reject");
        } else {
            metric_inc(MetricCounter::GatewayDisconnects);
            metric_inc(MetricCounter::QueueDrops);
            log(LogLevel::Error, "gateway_node", "failed to send response to OMS");
            break;
        }
    }

    MetricsRegistry::instance().write_jsonl("metrics/gateway_node.jsonl");
    replay_writer.close();

    stop_async_logger();
    return 0;
}