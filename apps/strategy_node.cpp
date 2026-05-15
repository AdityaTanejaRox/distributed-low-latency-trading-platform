#include "llt/logging.hpp"
#include "llt/metrics.hpp"
#include "llt/replay.hpp"
#include "llt/strategy.hpp"
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

        log(LogLevel::Warn, "strategy_node", "OMS unavailable; retrying");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main()
{
    start_async_logger("logs/strategy_node.log");
    pin_current_thread_to_cpu(1, "strategy-node");

    log(LogLevel::Info, "strategy_node", "starting distributed strategy node on port 21001");

    TcpConnection oms_conn = connect_retry("oms-node", 21002);

    TcpServer server;

    if (!server.listen_on(21001)) {
        log(LogLevel::Error, "strategy_node", "failed to listen for market data");
        MetricsRegistry::instance().write_jsonl("metrics/strategy_node.jsonl");
        stop_async_logger();
        return 1;
    }

    auto maybe_md = server.accept_one();

    if (!maybe_md) {
        log(LogLevel::Error, "strategy_node", "failed to accept market data connection");
        MetricsRegistry::instance().write_jsonl("metrics/strategy_node.jsonl");
        stop_async_logger();
        return 1;
    }

    TcpConnection md_conn = std::move(*maybe_md);

    StrategyEngine strategy{2};

    ReplayWriter replay_writer{
        "journals/replay_strategy.bin",
        ReplayWriteMode::Truncate,
        NodeRole::Strategy
    };

    replay_writer.open();

    Sequence signal_seq = 0;

    while (true) {
        auto maybe_msg = md_conn.recv_envelope();

        if (!maybe_msg) {
            log(LogLevel::Warn, "strategy_node", "market data node disconnected");
            break;
        }

        if (maybe_msg->type != MsgType::MarketData) {
            continue;
        }

        metric_inc(MetricCounter::MarketDataReceived);

        const auto& md = maybe_msg->payload.market_data;
        const TimestampNs now = now_ns();

        if (md.header.recv_ts_ns > 0 && now >= md.header.recv_ts_ns) {
            metric_latency(now - md.header.recv_ts_ns);
        }

        auto maybe_signal = strategy.on_market_data(md);

        if (!maybe_signal) {
            continue;
        }

        metric_inc(MetricCounter::SignalsGenerated);

        Envelope signal_env{};
        signal_env.type = MsgType::Signal;
        signal_env.payload.signal = *maybe_signal;

        replay_writer.append(signal_env, ++signal_seq, now_ns());
        metric_inc(MetricCounter::ReplayEventsWritten);

        if (oms_conn.send_envelope(signal_env, signal_seq)) {
            log(LogLevel::Info, "strategy_node", "sent Signal to OMS");
        } else {
            metric_inc(MetricCounter::QueueDrops);
            log(LogLevel::Error, "strategy_node", "failed to send Signal to OMS");
            break;
        }
    }

    MetricsRegistry::instance().write_jsonl("metrics/strategy_node.jsonl");
    replay_writer.close();

    stop_async_logger();
    return 0;
}