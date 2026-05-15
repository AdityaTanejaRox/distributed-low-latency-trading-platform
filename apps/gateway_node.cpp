#include "llt/exchange_gateway.hpp"
#include "llt/kraken_futures_demo_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/metrics.hpp"
#include "llt/replay.hpp"
#include "llt/tcp_transport.hpp"
#include "llt/threading.hpp"
#include "llt/time.hpp"

#include <cstdlib>
#include <optional>
#include <string>

using namespace llt;

namespace
{
    std::string getenv_or_default(const char* name, const std::string& fallback)
    {
        if (const char* value = std::getenv(name)) {
            return value;
        }

        return fallback;
    }

    Envelope make_ack_from_order(const NewOrder& order, OrderId exchange_order_id)
    {
        Ack ack{};
        ack.header.type = MsgType::Ack;
        ack.header.source_node = 4;
        ack.header.destination_node = 3;
        ack.header.sequence = order.header.sequence;
        ack.header.send_ts_ns = now_ns();

        ack.client_order_id = order.client_order_id;
        ack.exchange_order_id = exchange_order_id;
        ack.state = OrderState::Accepted;

        Envelope env{};
        env.type = MsgType::Ack;
        env.payload.ack = ack;

        return env;
    }

    Envelope make_reject_from_order(const NewOrder& order, RejectReason reason)
    {
        Reject reject{};
        reject.header.type = MsgType::Reject;
        reject.header.source_node = 4;
        reject.header.destination_node = 3;
        reject.header.sequence = order.header.sequence;
        reject.header.send_ts_ns = now_ns();

        reject.client_order_id = order.client_order_id;
        reject.reason = reason;

        Envelope env{};
        env.type = MsgType::Reject;
        env.payload.reject = reject;

        return env;
    }
}

int main()
{
    start_async_logger("logs/gateway_node.log");
    pin_current_thread_to_cpu(3, "gateway-node");

    const std::string mode = getenv_or_default("LLT_GATEWAY_MODE", "kraken");
    const std::string kraken_symbol = getenv_or_default("LLT_KRAKEN_SYMBOL", "PI_XBTUSD");

    log(LogLevel::Info, "gateway_node", "starting distributed gateway node on port 21003");

    std::optional<KrakenFuturesDemoGateway> kraken_gateway;

    if (mode == "kraken") {
        auto creds = load_kraken_demo_credentials_from_env();

        if (!creds) {
            log(LogLevel::Error, "gateway_node", "LLT_GATEWAY_MODE=kraken but Kraken credentials are missing");
            MetricsRegistry::instance().write_jsonl("metrics/gateway_node.jsonl");
            stop_async_logger();
            return 1;
        }

        kraken_gateway.emplace(*creds);
        log(LogLevel::Info, "gateway_node", "gateway running in Kraken Futures Demo mode");
    } else if (mode == "sim") {
        log(LogLevel::Warn, "gateway_node", "gateway running in explicit simulated mode");
    } else {
        log(LogLevel::Error, "gateway_node", "unknown LLT_GATEWAY_MODE");
        MetricsRegistry::instance().write_jsonl("metrics/gateway_node.jsonl");
        stop_async_logger();
        return 1;
    }

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

    ExchangeGateway simulated_gateway{4};
    Sequence response_seq = 0;
    OrderId synthetic_exchange_id = 50'000;

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

        const NewOrder& order = maybe_msg->payload.new_order;
        metric_inc(MetricCounter::OrdersSent);

        Envelope response{};

        if (mode == "kraken") {
            const auto result =
                kraken_gateway->send_internal_order_as_demo_limit(order, kraken_symbol);

            if (result.kraken_success) {
                response = make_ack_from_order(order, ++synthetic_exchange_id);
                metric_inc(MetricCounter::AcksReceived);
                log(LogLevel::Info, "gateway_node", "Kraken Futures Demo accepted order");
            } else {
                response = make_reject_from_order(order, RejectReason::GatewayUnavailable);
                metric_inc(MetricCounter::OrdersRejected);
                log(LogLevel::Error, "gateway_node", result.raw_response);
            }
        } else {
            auto simulated_response = simulated_gateway.send_order(order);

            if (!simulated_response) {
                response = make_reject_from_order(order, RejectReason::GatewayUnavailable);
                metric_inc(MetricCounter::OrdersRejected);
            } else {
                response = *simulated_response;

                if (response.type == MsgType::Ack) {
                    metric_inc(MetricCounter::AcksReceived);
                } else if (response.type == MsgType::Reject) {
                    metric_inc(MetricCounter::OrdersRejected);
                }
            }
        }

        ++response_seq;

        if (response.type == MsgType::Ack) {
            response.payload.ack.header.sequence = response_seq;
            response.payload.ack.header.send_ts_ns = now_ns();
        } else if (response.type == MsgType::Reject) {
            response.payload.reject.header.sequence = response_seq;
            response.payload.reject.header.send_ts_ns = now_ns();
        } else if (response.type == MsgType::Fill) {
            response.payload.fill.header.sequence = response_seq;
            response.payload.fill.header.send_ts_ns = now_ns();
        }

        replay_writer.append(response, response_seq, now_ns());

        if (!conn.send_envelope(response, response_seq)) {
            metric_inc(MetricCounter::GatewayDisconnects);
            metric_inc(MetricCounter::QueueDrops);
            log(LogLevel::Error, "gateway_node", "failed to send gateway response to OMS");
            break;
        }

        log(LogLevel::Info, "gateway_node", "processed NewOrder and returned exchange response");

        if (response_seq % 25 == 0) {
            MetricsRegistry::instance().write_jsonl("metrics/gateway_node.jsonl");
        }
    }

    MetricsRegistry::instance().write_jsonl("metrics/gateway_node.jsonl");
    replay_writer.close();

    stop_async_logger();
    return 0;
}