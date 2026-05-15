#pragma once

#include "llt/market_data_adapter.hpp"

#include <functional>
#include <string>

namespace llt
{
    using MarketDataCallback = std::function<void(const NormalizedMarketData&)>;

    bool run_binance_live_book_ticker(
        const std::string& symbol_lower,
        int max_updates,
        MarketDataCallback callback = {},
        SymbolId symbol_id = 1
    );

    bool run_coinbase_live_ticker(
        const std::string& product_id,
        int max_updates,
        MarketDataCallback callback = {},
        SymbolId symbol_id = 1
    );

    bool run_hyperliquid_live_l2book(
        const std::string& coin,
        int max_updates,
        MarketDataCallback callback = {},
        SymbolId symbol_id = 1
    );
}