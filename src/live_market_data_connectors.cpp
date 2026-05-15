#include "llt/live_market_data_connectors.hpp"
#include "llt/logging.hpp"
#include "llt/ws_client.hpp"

#include <iostream>
#include <string>

namespace llt
{
    namespace
    {
        void print_md(const NormalizedMarketData& md)
        {
            const auto& u = md.update;

            std::cout
                << "venue=" << to_string(md.venue)
                << " symbol=" << u.symbol.str()
                << " seq=" << u.exchange_sequence
                << " bid=" << u.bid_px
                << " bid_qty=" << u.bid_qty
                << " ask=" << u.ask_px
                << " ask_qty=" << u.ask_qty
                << '\n';
        }

        void emit_md(
            const NormalizedMarketData& md,
            const MarketDataCallback& callback
        )
        {
            if (callback)
            {
                callback(md);
            }
            else
            {
                print_md(md);
            }
        }
    }

    bool run_binance_live_book_ticker(
        const std::string& symbol_lower,
        int max_updates,
        MarketDataCallback callback,
        SymbolId symbol_id
    )
    {
        const std::string target = "/ws/" + symbol_lower + "@bookTicker";

        WssClient client{"data-stream.binance.vision", "443", target};

        if (!client.connect())
        {
            log(LogLevel::Error, "binance_live", "failed to connect websocket");
            return false;
        }

        log(LogLevel::Info, "binance_live", "connected to Binance bookTicker stream");

        int received = 0;

        while (max_updates == 0 || received < max_updates)
        {
            std::string raw;

            if (!client.read_text(raw))
            {
                log(LogLevel::Warn, "binance_live", "websocket read failed or connection closed");
                break;
            }

            auto md = normalize_binance_book_ticker(raw, symbol_id);

            if (!md)
            {
                log(LogLevel::Warn, "binance_live", "failed to normalize Binance frame");
                continue;
            }

            emit_md(*md, callback);

            log(LogLevel::Info, "binance_live", "normalized live Binance bookTicker");
            ++received;
        }

        client.close();
        return received > 0;
    }

    bool run_coinbase_live_ticker(
        const std::string& product_id,
        int max_updates,
        MarketDataCallback callback,
        SymbolId symbol_id
    )
    {
        WssClient client{"ws-feed.exchange.coinbase.com", "443", "/"};

        if (!client.connect())
        {
            log(LogLevel::Error, "coinbase_live", "failed to connect websocket");
            return false;
        }

        const std::string subscribe =
            R"({"type":"subscribe","product_ids":[")" +
            product_id +
            R"("],"channels":["ticker"]})";

        if (!client.write_text(subscribe))
        {
            log(LogLevel::Error, "coinbase_live", "failed to send subscription");
            client.close();
            return false;
        }

        log(LogLevel::Info, "coinbase_live", "sent Coinbase ticker subscription");

        int received = 0;

        while (max_updates == 0 || received < max_updates)
        {
            std::string raw;

            if (!client.read_text(raw))
            {
                log(LogLevel::Warn, "coinbase_live", "websocket read failed or connection closed");
                break;
            }

            auto md = normalize_coinbase_ticker(raw, symbol_id);

            if (!md)
            {
                // Coinbase sends subscription confirmations and heartbeat-ish
                // control messages. Those are expected to not normalize.
                log(LogLevel::Info, "coinbase_live", "skipped non-ticker Coinbase frame");
                continue;
            }

            emit_md(*md, callback);

            log(LogLevel::Info, "coinbase_live", "normalized live Coinbase ticker");
            ++received;
        }

        client.close();
        return received > 0;
    }

    bool run_hyperliquid_live_l2book(
        const std::string& coin,
        int max_updates,
        MarketDataCallback callback,
        SymbolId symbol_id
    )
    {
        WssClient client{"api.hyperliquid.xyz", "443", "/ws"};

        if (!client.connect())
        {
            log(LogLevel::Error, "hyperliquid_live", "failed to connect websocket");
            return false;
        }

        const std::string subscribe =
            R"({"method":"subscribe","subscription":{"type":"l2Book","coin":")" +
            coin +
            R"("}})";

        if (!client.write_text(subscribe))
        {
            log(LogLevel::Error, "hyperliquid_live", "failed to send subscription");
            client.close();
            return false;
        }

        log(LogLevel::Info, "hyperliquid_live", "sent Hyperliquid l2Book subscription");

        int received = 0;

        while (max_updates == 0 || received < max_updates)
        {
            std::string raw;

            if (!client.read_text(raw))
            {
                log(LogLevel::Warn, "hyperliquid_live", "websocket read failed or connection closed");
                break;
            }

            auto md = normalize_hyperliquid_l2book(raw, symbol_id);

            if (!md)
            {
                log(LogLevel::Info, "hyperliquid_live", "skipped non-l2Book Hyperliquid frame");
                continue;
            }

            emit_md(*md, callback);

            log(LogLevel::Info, "hyperliquid_live", "normalized live Hyperliquid l2Book");
            ++received;
        }

        client.close();
        return received > 0;
    }
}