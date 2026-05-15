#pragma once

#include "llt/types.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace llt
{
    struct SymbolBookSnapshot
    {
        SymbolId symbol_id{0};
        Symbol symbol{};

        Price bid_px{0};
        Quantity bid_qty{0};

        Price ask_px{0};
        Quantity ask_qty{0};

        Sequence exchange_sequence{0};
        TimestampNs recv_ts_ns{0};
    };

    class MultiSymbolTopOfBook
    {
    public:
        bool apply(const MarketDataUpdate& update);

        std::optional<SymbolBookSnapshot> snapshot(SymbolId symbol_id) const;

        std::vector<SymbolBookSnapshot> snapshots() const;

        std::size_t symbol_count() const;

        void clear();

    private:
        std::unordered_map<SymbolId, SymbolBookSnapshot> books_;
    };
}