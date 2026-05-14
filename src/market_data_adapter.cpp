#include "llt/market_data_adapter.hpp"
#include "llt/logging.hpp"
#include "llt/time.hpp"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace llt 
{

    namespace 
    {

        // ============================================================
        // Lightweight Raw Field Extraction
        // ============================================================
        //
        // For this phase, we intentionally avoid adding a JSON dependency.
        //
        // Why?
        //
        //   - This project is focused on the low-latency architecture.
        //   - We only need a small number of known fields.
        //   - Full JSON parsing is slower and may allocate.
        //   - Real production code would use a carefully selected parser
        //     or venue-native binary protocol.
        //
        // These helpers are NOT a general JSON parser.
        // They are compact field extractors for known demo messages.
        // ============================================================

        std::optional<std::string> extract_json_string(
            std::string_view raw,
            std::string_view key
        ) 
        {
            const std::string pattern = "\"" + std::string(key) + "\"";
            const auto key_pos = raw.find(pattern);

            if (key_pos == std::string_view::npos) 
            {
                return std::nullopt;
            }

            const auto colon_pos = raw.find(':', key_pos + pattern.size());

            if (colon_pos == std::string_view::npos) 
            {
                return std::nullopt;
            }

            const auto first_quote = raw.find('"', colon_pos + 1);

            if (first_quote == std::string_view::npos) 
            {
                return std::nullopt;
            }

            const auto second_quote = raw.find('"', first_quote + 1);

            if (second_quote == std::string_view::npos) 
            {
                return std::nullopt;
            }

            return std::string
            {
                raw.substr(first_quote + 1, second_quote - first_quote - 1)
            };
        }

        std::optional<std::string> extract_json_number_as_string(
            std::string_view raw,
            std::string_view key
        ) 
        {
            const std::string pattern = "\"" + std::string(key) + "\"";
            const auto key_pos = raw.find(pattern);

            if (key_pos == std::string_view::npos) 
            {
                return std::nullopt;
            }

            const auto colon_pos = raw.find(':', key_pos + pattern.size());

            if (colon_pos == std::string_view::npos) 
            {
                return std::nullopt;
            }

            std::size_t start = colon_pos + 1;

            while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start]))) 
            {
                ++start;
            }

            std::size_t end = start;

            while (end < raw.size()) 
            {
                const char c = raw[end];

                if (!(std::isdigit(static_cast<unsigned char>(c)) ||
                    c == '.' ||
                    c == '-')) 
                {
                    break;
                }

                ++end;
            }

            if (end == start) 
            {
                return std::nullopt;
            }

            return std::string{raw.substr(start, end - start)};
        }

        std::optional<Sequence> parse_sequence(std::string_view value) 
        {
            Sequence result = 0;

            const auto* begin = value.data();
            const auto* end = value.data() + value.size();

            const auto [ptr, ec] = std::from_chars(begin, end, result);

            if (ec != std::errc{}) 
            {
                return std::nullopt;
            }

            return result;
        }

        std::vector<std::string> split_pipe_line(std::string_view line) 
        {
            std::vector<std::string> parts;
            std::size_t start = 0;

            while (start <= line.size()) 
            {
                const auto pos = line.find('|', start);

                if (pos == std::string_view::npos) 
                {
                    parts.emplace_back(line.substr(start));
                    break;
                }

                parts.emplace_back(line.substr(start, pos - start));
                start = pos + 1;
            }

            return parts;
        }

        MarketDataUpdate make_update(
            MarketDataVenue venue,
            SymbolId symbol_id,
            std::string_view symbol,
            Price bid_px,
            Quantity bid_qty,
            Price ask_px,
            Quantity ask_qty,
            Sequence exchange_sequence
        ) 
        {
            MarketDataUpdate update{};

            update.header.type = MsgType::MarketData;
            update.header.source_node = static_cast<NodeId>(venue);
            update.header.destination_node = 2;
            update.header.sequence = exchange_sequence;
            update.header.send_ts_ns = now_ns();
            update.header.recv_ts_ns = now_ns();

            update.symbol_id = symbol_id;
            update.symbol = Symbol{symbol};
            update.bid_px = bid_px;
            update.bid_qty = bid_qty;
            update.ask_px = ask_px;
            update.ask_qty = ask_qty;
            update.exchange_sequence = exchange_sequence;

            return update;
        }

    } // namespace

    Price parse_price_to_ticks(std::string_view value, int scale) 
    {
        // Convert using simple deterministic decimal parsing.
        //
        // Example:
        //
        //   value = "189.25"
        //   scale = 100
        //   result = 18925
        //
        // This avoids floating point in the normalized model.

        bool negative = false;

        if (!value.empty() && value.front() == '-') 
        {
            negative = true;
            value.remove_prefix(1);
        }

        std::int64_t whole = 0;
        std::int64_t fractional = 0;
        int fractional_digits = 0;

        bool after_decimal = false;

        for (char c : value) 
        {
            if (c == '.') 
            {
                after_decimal = true;
                continue;
            }

            if (!std::isdigit(static_cast<unsigned char>(c))) 
            {
                break;
            }

            const int digit = c - '0';

            if (!after_decimal) 
            {
                whole = whole * 10 + digit;
            } 
            else if (fractional_digits < 9) 
            {
                fractional = fractional * 10 + digit;
                ++fractional_digits;
            }
        }

        while (fractional_digits < 2) 
        {
            fractional *= 10;
            ++fractional_digits;
        }

        // This demo assumes price scale = 100.
        // Later we can pass per-symbol scales for crypto/equities.
        std::int64_t result = whole * scale;

        if (scale == 100) 
        {
            result += fractional / 1;
        }

        return negative ? -result : result;
    }

    Quantity parse_quantity_to_units(std::string_view value) 
    {
        // Normalize fractional crypto/equity sizes into integer micro-units.
        //
        // Example:
        //   "0.001234" -> 1234
        //   "1.500000" -> 1500000
        //
        // This keeps the internal model integer-only while preserving
        // fractional venue sizes.

        constexpr Quantity SCALE = 1'000'000;

        bool negative = false;

        if (!value.empty() && value.front() == '-') 
        {
            negative = true;
            value.remove_prefix(1);
        }

        Quantity whole = 0;
        Quantity fractional = 0;
        int fractional_digits = 0;
        bool after_decimal = false;

        for (char c : value) 
        {
            if (c == '.') 
            {
                after_decimal = true;
                continue;
            }

            if (!std::isdigit(static_cast<unsigned char>(c))) 
            {
                break;
            }

            const int digit = c - '0';

            if (!after_decimal) 
            {
                whole = whole * 10 + digit;
            } 
            else if (fractional_digits < 6) 
            {
                fractional = fractional * 10 + digit;
                ++fractional_digits;
            }
        }

        while (fractional_digits < 6) 
        {
            fractional *= 10;
            ++fractional_digits;
        }

        Quantity result = whole * SCALE + fractional;
        return negative ? -result : result;
    }

    std::optional<NormalizedMarketData> normalize_binance_book_ticker(
        std::string_view raw_json,
        SymbolId symbol_id
    ) 
    {
        const auto symbol = extract_json_string(raw_json, "s");
        const auto seq = extract_json_number_as_string(raw_json, "u");
        const auto bid_px = extract_json_string(raw_json, "b");
        const auto bid_qty = extract_json_string(raw_json, "B");
        const auto ask_px = extract_json_string(raw_json, "a");
        const auto ask_qty = extract_json_string(raw_json, "A");

        if (!symbol || !seq || !bid_px || !bid_qty || !ask_px || !ask_qty) 
        {
            log(LogLevel::Warn, "md_binance", "failed to parse Binance bookTicker");
            return std::nullopt;
        }

        const auto sequence = parse_sequence(*seq);

        if (!sequence) 
        {
            log(LogLevel::Warn, "md_binance", "failed to parse Binance sequence");
            return std::nullopt;
        }

        NormalizedMarketData out{};
        out.venue = MarketDataVenue::Binance;
        out.update = make_update(
            MarketDataVenue::Binance,
            symbol_id,
            *symbol,
            parse_price_to_ticks(*bid_px),
            parse_quantity_to_units(*bid_qty),
            parse_price_to_ticks(*ask_px),
            parse_quantity_to_units(*ask_qty),
            *sequence
        );

        return out;
    }

    std::optional<NormalizedMarketData> normalize_coinbase_ticker(
        std::string_view raw_json,
        SymbolId symbol_id
    ) 
    {
        const auto symbol = extract_json_string(raw_json, "product_id");
        const auto seq = extract_json_number_as_string(raw_json, "sequence");
        const auto bid_px = extract_json_string(raw_json, "best_bid");
        const auto bid_qty = extract_json_string(raw_json, "best_bid_size");
        const auto ask_px = extract_json_string(raw_json, "best_ask");
        const auto ask_qty = extract_json_string(raw_json, "best_ask_size");

        if (!symbol || !seq || !bid_px || !bid_qty || !ask_px || !ask_qty) 
        {
            log(LogLevel::Warn, "md_coinbase", "failed to parse Coinbase ticker");
            return std::nullopt;
        }

        const auto sequence = parse_sequence(*seq);

        if (!sequence) 
        {
            log(LogLevel::Warn, "md_coinbase", "failed to parse Coinbase sequence");
            return std::nullopt;
        }

        NormalizedMarketData out{};
        out.venue = MarketDataVenue::Coinbase;
        out.update = make_update(
            MarketDataVenue::Coinbase,
            symbol_id,
            *symbol,
            parse_price_to_ticks(*bid_px),
            parse_quantity_to_units(*bid_qty),
            parse_price_to_ticks(*ask_px),
            parse_quantity_to_units(*ask_qty),
            *sequence
        );

        return out;
    }

    std::optional<NormalizedMarketData> normalize_polygon_quote(
        std::string_view raw_json,
        SymbolId symbol_id
    ) 
    {
        const auto symbol = extract_json_string(raw_json, "sym");
        const auto seq = extract_json_number_as_string(raw_json, "q");
        const auto bid_px = extract_json_number_as_string(raw_json, "bp");
        const auto bid_qty = extract_json_number_as_string(raw_json, "bs");
        const auto ask_px = extract_json_number_as_string(raw_json, "ap");
        const auto ask_qty = extract_json_number_as_string(raw_json, "as");

        if (!symbol || !seq || !bid_px || !bid_qty || !ask_px || !ask_qty) 
        {
            log(LogLevel::Warn, "md_polygon", "failed to parse Polygon quote");
            return std::nullopt;
        }

        const auto sequence = parse_sequence(*seq);

        if (!sequence) 
        {
            log(LogLevel::Warn, "md_polygon", "failed to parse Polygon sequence");
            return std::nullopt;
        }

        NormalizedMarketData out{};
        out.venue = MarketDataVenue::Polygon;
        out.update = make_update(
            MarketDataVenue::Polygon,
            symbol_id,
            *symbol,
            parse_price_to_ticks(*bid_px),
            parse_quantity_to_units(*bid_qty),
            parse_price_to_ticks(*ask_px),
            parse_quantity_to_units(*ask_qty),
            *sequence
        );

        return out;
    }

    std::optional<NormalizedMarketData> normalize_simulated_itch_quote(
        std::string_view raw_line,
        SymbolId symbol_id
    ) 
    {
        const auto parts = split_pipe_line(raw_line);

        // Expected:
        //
        //   Q|sequence|symbol|bid_px|bid_qty|ask_px|ask_qty
        if (parts.size() != 7 || parts[0] != "Q") 
        {
            log(LogLevel::Warn, "md_itch", "failed to parse ITCH quote");
            return std::nullopt;
        }

        const auto sequence = parse_sequence(parts[1]);

        if (!sequence) 
        {
            log(LogLevel::Warn, "md_itch", "failed to parse ITCH sequence");
            return std::nullopt;
        }

        NormalizedMarketData out{};
        out.venue = MarketDataVenue::SimulatedItch;
        out.update = make_update(
            MarketDataVenue::SimulatedItch,
            symbol_id,
            parts[2],
            static_cast<Price>(std::stoll(parts[3])),
            static_cast<Quantity>(std::stoll(parts[4])),
            static_cast<Price>(std::stoll(parts[5])),
            static_cast<Quantity>(std::stoll(parts[6])),
            *sequence
        );

        return out;
    }

    std::optional<NormalizedMarketData> normalize_hyperliquid_l2book(
    std::string_view raw_json,
    SymbolId symbol_id
    ) 
    {
        // Hyperliquid l2Book shape:
        //
        // {
        //   "channel": "l2Book",
        //   "data": {
        //     "coin": "BTC",
        //     "time": ...,
        //     "levels": [
        //       [ { "px": "...", "sz": "...", ... } ],   // bids
        //       [ { "px": "...", "sz": "...", ... } ]    // asks
        //     ]
        //   }
        // }
        //
        // This extractor intentionally grabs only top-of-book:
        //   best bid = first object in levels[0]
        //   best ask = first object in levels[1]

        if (raw_json.find(R"("channel":"l2Book")") == std::string_view::npos) 
        {
            return std::nullopt;
        }

        const auto coin = extract_json_string(raw_json, "coin");
        const auto time = extract_json_number_as_string(raw_json, "time");

        if (!coin || !time) 
        {
            log(LogLevel::Warn, "md_hyperliquid", "failed to parse Hyperliquid coin/time");
            return std::nullopt;
        }

        const auto levels_pos = raw_json.find(R"("levels")");
        if (levels_pos == std::string_view::npos) 
        {
            return std::nullopt;
        }

        const auto first_px_pos = raw_json.find(R"("px")", levels_pos);
        const auto first_sz_pos = raw_json.find(R"("sz")", first_px_pos);

        if (first_px_pos == std::string_view::npos || first_sz_pos == std::string_view::npos) 
        {
            return std::nullopt;
        }

        const auto bid_px = extract_json_string(raw_json.substr(first_px_pos), "px");
        const auto bid_sz = extract_json_string(raw_json.substr(first_sz_pos), "sz");

        const auto second_array_pos = raw_json.find("],[", first_sz_pos);
        if (second_array_pos == std::string_view::npos) 
        {
            return std::nullopt;
        }

        const auto ask_px_pos = raw_json.find(R"("px")", second_array_pos);
        const auto ask_sz_pos = raw_json.find(R"("sz")", ask_px_pos);

        if (ask_px_pos == std::string_view::npos || ask_sz_pos == std::string_view::npos) 
        {
            return std::nullopt;
        }

        const auto ask_px = extract_json_string(raw_json.substr(ask_px_pos), "px");
        const auto ask_sz = extract_json_string(raw_json.substr(ask_sz_pos), "sz");

        const auto sequence = parse_sequence(*time);

        if (!bid_px || !bid_sz || !ask_px || !ask_sz || !sequence) 
        {
            log(LogLevel::Warn, "md_hyperliquid", "failed to parse Hyperliquid top of book");
            return std::nullopt;
        }

        NormalizedMarketData out{};
        out.venue = MarketDataVenue::Hyperliquid;
        out.update = make_update(
            MarketDataVenue::Hyperliquid,
            symbol_id,
            *coin,
            parse_price_to_ticks(*bid_px),
            parse_quantity_to_units(*bid_sz),
            parse_price_to_ticks(*ask_px),
            parse_quantity_to_units(*ask_sz),
            *sequence
        );

        return out;
    }

    const char* to_string(MarketDataVenue venue) 
    {
        switch (venue) 
        {
            case MarketDataVenue::Binance: return "Binance";
            case MarketDataVenue::Coinbase: return "Coinbase";
            case MarketDataVenue::Polygon: return "Polygon";
            case MarketDataVenue::Hyperliquid: return "Hyperliquid";
            case MarketDataVenue::SimulatedItch: return "SimulatedItch";
        }

        return "Unknown";
    }

}