#pragma once

#include "llt/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace llt 
{

    // ============================================================
    // Phase 4: Market Data Adapter Layer
    // ============================================================
    //
    // External venues do NOT speak our internal format.
    //
    // Binance, Coinbase, Polygon, and ITCH-style feeds all expose different
    // field names, sequence models, price formats, and symbol conventions.
    //
    // The adapter layer converts:
    //
    //   venue-native raw message
    //        ->
    //   normalized internal MarketDataUpdate
    //
    // This keeps strategy code venue-agnostic.
    //
    // Strategy should not care whether data came from:
    //
    //   - Binance crypto WebSocket
    //   - Coinbase crypto WebSocket
    //   - Polygon/Massive equities quote stream
    //   - simulated ITCH-style feed
    //
    // Strategy only consumes:
    //
    //   MarketDataUpdate
    // ============================================================

    enum class MarketDataVenue : std::uint8_t 
    {
        Binance = 1,
        Coinbase = 2,
        Polygon = 3,
        Hyperliquid = 4,
        SimulatedItch = 5
    };

    struct NormalizedMarketData 
    {
        MarketDataVenue venue{MarketDataVenue::Binance};
        MarketDataUpdate update{};
    };

    // Converts decimal price text into integer ticks.
    //
    // Example with scale=100:
    //
    //   "100.25" -> 10025
    //
    // Avoid double in the normalized hot path because binary floating point
    // can introduce rounding surprises and non-deterministic formatting behavior.
    Price parse_price_to_ticks(std::string_view value, int scale = 100);

    // Converts decimal quantity text into integer units.
    //
    // For demo simplicity, we scale quantity by 1.
    // Later, each venue/symbol can define its own quantity scale.
    Quantity parse_quantity_to_units(std::string_view value);

    // Binance bookTicker sample fields:
    //
    //   s = symbol
    //   u = update id / sequence
    //   b = best bid price
    //   B = best bid quantity
    //   a = best ask price
    //   A = best ask quantity
    std::optional<NormalizedMarketData> normalize_binance_book_ticker(
        std::string_view raw_json,
        SymbolId symbol_id
    );

    // Coinbase Exchange ticker sample fields:
    //
    //   product_id = symbol
    //   sequence = sequence number
    //   best_bid = best bid price
    //   best_bid_size = best bid quantity
    //   best_ask = best ask price
    //   best_ask_size = best ask quantity
    std::optional<NormalizedMarketData> normalize_coinbase_ticker(
        std::string_view raw_json,
        SymbolId symbol_id
    );

    // Polygon/Massive stocks quote sample fields:
    //
    //   sym = symbol
    //   q   = sequence number
    //   bp  = bid price
    //   bs  = bid size
    //   ap  = ask price
    //   as  = ask size
    std::optional<NormalizedMarketData> normalize_polygon_quote(
        std::string_view raw_json,
        SymbolId symbol_id
    );

    // Simulated ITCH-style quote line:
    //
    //   Q|sequence|symbol|bid_px|bid_qty|ask_px|ask_qty
    //
    // Example:
    //
    //   Q|1001|AAPL|18925|100|18926|200
    std::optional<NormalizedMarketData> normalize_simulated_itch_quote(
        std::string_view raw_line,
        SymbolId symbol_id
    );

    std::optional<NormalizedMarketData> normalize_hyperliquid_l2book(
        std::string_view raw_json,
        SymbolId symbol_id
    );

    const char* to_string(MarketDataVenue venue);
}