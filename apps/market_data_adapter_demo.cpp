#include "llt/logging.hpp"
#include "llt/market_data_adapter.hpp"

#include <iostream>
#include <optional>

using namespace llt;

// ============================================================
// Phase 4 Market Data Adapter Demo
// ============================================================
//
// This demo validates venue-specific normalization.
//
// We feed representative raw messages from:
//
//   1. Binance bookTicker
//   2. Coinbase ticker
//   3. Polygon/Massive stock quote
//   4. Simulated ITCH-style quote
//
// Each adapter emits the same internal structure:
//
//   MarketDataUpdate
//
// This proves the strategy layer can consume one format regardless of venue.
// ============================================================

void print_normalized(const NormalizedMarketData& md) 
{
    const auto& u = md.update;

    std::cout
        << "venue=" << to_string(md.venue)
        << " symbol=" << u.symbol.str()
        << " sequence=" << u.exchange_sequence
        << " bid_px=" << u.bid_px
        << " bid_qty=" << u.bid_qty
        << " ask_px=" << u.ask_px
        << " ask_qty=" << u.ask_qty
        << '\n';
}

int main() 
{
    start_async_logger("logs/market_data_adapter_demo.log");

    log(LogLevel::Info, "md_adapter_demo", "starting phase 4 market data adapter demo");

    // Binance representative bookTicker payload.
    //
    // Important fields:
    //   u = update id
    //   s = symbol
    //   b = best bid price
    //   B = best bid quantity
    //   a = best ask price
    //   A = best ask quantity
    const std::string binance_raw =
        R"({"u":400900217,"s":"BTCUSDT","b":"64250.12","B":"3.5","a":"64250.13","A":"2.1"})";

    // Coinbase representative ticker payload.
    //
    // Important fields:
    //   product_id
    //   sequence
    //   best_bid
    //   best_bid_size
    //   best_ask
    //   best_ask_size
    const std::string coinbase_raw =
        R"({"type":"ticker","sequence":7654321,"product_id":"BTC-USD","price":"64250.12","best_bid":"64249.90","best_bid_size":"1.0","best_ask":"64250.10","best_ask_size":"2.0"})";

    // Polygon/Massive representative equity quote payload.
    //
    // Important fields:
    //   ev  = quote event
    //   sym = ticker symbol
    //   q   = sequence number
    //   bp  = bid price
    //   bs  = bid size
    //   ap  = ask price
    //   as  = ask size
    const std::string polygon_raw =
        R"({"ev":"Q","sym":"AAPL","q":9001001,"bp":189.25,"bs":100,"ap":189.26,"as":200})";

    // Simulated ITCH-style top-of-book quote.
    //
    // Format:
    //   Q|sequence|symbol|bid_px|bid_qty|ask_px|ask_qty
    //
    // ITCH-style protocols are usually binary and sequence-heavy.
    // This text version lets us model the same concept simply.
    const std::string itch_raw =
        "Q|1001|MSFT|42125|300|42126|400";

    const auto binance = normalize_binance_book_ticker(binance_raw, 1);
    const auto coinbase = normalize_coinbase_ticker(coinbase_raw, 2);
    const auto polygon = normalize_polygon_quote(polygon_raw, 3);
    const auto itch = normalize_simulated_itch_quote(itch_raw, 4);

    if (binance) 
    {
        print_normalized(*binance);
        log(LogLevel::Info, "md_adapter_demo", "normalized Binance bookTicker");
    }

    if (coinbase) 
    {
        print_normalized(*coinbase);
        log(LogLevel::Info, "md_adapter_demo", "normalized Coinbase ticker");
    }

    if (polygon) 
    {
        print_normalized(*polygon);
        log(LogLevel::Info, "md_adapter_demo", "normalized Polygon quote");
    }

    if (itch) 
    {
        print_normalized(*itch);
        log(LogLevel::Info, "md_adapter_demo", "normalized simulated ITCH quote");
    }

    log(LogLevel::Info, "md_adapter_demo", "phase 4 market data adapter demo complete");

    stop_async_logger();
    return 0;
}