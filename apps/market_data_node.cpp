#include "llt/live_market_data_connectors.hpp"
#include "llt/logging.hpp"
#include "llt/order_book.hpp"
#include "llt/tcp_transport.hpp"
#include "llt/threading.hpp"
#include "llt/replay.hpp"
#include "llt/time.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace llt;

static TcpConnection connect_retry(const std::string& host, std::uint16_t port)
{
    while (true) {
        auto conn = TcpClient::connect_to(host, port);

        if (conn) {
            return std::move(*conn);
        }

        log(LogLevel::Warn, "market_data_node", "strategy unavailable; retrying");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char** argv)
{
    start_async_logger("logs/market_data_node.log");
    pin_current_thread_to_cpu(0, "md-node");

    std::string venue = "coinbase";

    if (argc >= 2) {
        venue = argv[1];
    }

    if (const char* env_venue = std::getenv("LLT_VENUE")) {
        venue = env_venue;
    }

    log(LogLevel::Info, "market_data_node", "starting distributed market data node");

    TcpConnection strategy_conn = connect_retry("strategy-node", 21001);

    TopOfBook book;
    Sequence outbound_seq = 0;

    ReplayWriter replay_writer{
        "journals/replay_market_data.bin",
        ReplayWriteMode::Truncate,
        NodeRole::MarketData
    };

    replay_writer.open();

    auto callback = [&](const NormalizedMarketData& md) {
        const auto& u = md.update;

        book.apply(u);

        if (auto snapshot = book.snapshot()) {
            std::cout
                << "md_node book venue=" << to_string(md.venue)
                << " symbol=" << snapshot->symbol.str()
                << " seq=" << snapshot->exchange_sequence
                << " bid=" << snapshot->bid_px
                << " ask=" << snapshot->ask_px
                << '\n';
        }

        Envelope env{};
        env.type = MsgType::MarketData;
        env.payload.market_data = u;

        replay_writer.append(
            env,
            ++outbound_seq,
            now_ns()
        );

        if (!strategy_conn.send_envelope(env, outbound_seq)) {
            log(LogLevel::Error, "market_data_node", "failed to send MarketDataUpdate to strategy");
        }
    };

    if (venue == "binance") {
        run_binance_live_book_ticker("btcusdt", 0, callback);
    } else if (venue == "coinbase") {
        run_coinbase_live_ticker("BTC-USD", 0, callback);
    } else if (venue == "hyperliquid") {
        run_hyperliquid_live_l2book("BTC", 0, callback);
    } else {
        log(LogLevel::Error, "market_data_node", "unknown LLT_VENUE");
        stop_async_logger();
        return 1;
    }

    replay_writer.close();

    stop_async_logger();
    return 0;
}