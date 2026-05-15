#pragma once

#include "llt/market_data_adapter.hpp"
#include "llt/types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace llt
{
    struct VenueBookKey
    {
        MarketDataVenue venue{MarketDataVenue::Coinbase};
        SymbolId symbol_id{0};

        bool operator==(const VenueBookKey& other) const
        {
            return venue == other.venue && symbol_id == other.symbol_id;
        }
    };

    struct VenueBookKeyHash
    {
        std::size_t operator()(const VenueBookKey& key) const
        {
            return
                (static_cast<std::size_t>(key.symbol_id) << 8) ^
                static_cast<std::size_t>(key.venue);
        }
    };

    struct VenueTopOfBook
    {
        MarketDataVenue venue{MarketDataVenue::Coinbase};
        SymbolId symbol_id{0};
        Symbol symbol{};

        Price bid_px{0};
        Quantity bid_qty{0};

        Price ask_px{0};
        Quantity ask_qty{0};

        Sequence exchange_sequence{0};
        TimestampNs recv_ts_ns{0};
    };

    struct RouteDecision
    {
        bool routeable{false};
        MarketDataVenue venue{MarketDataVenue::Coinbase};
        SymbolId symbol_id{0};
        Symbol symbol{};
        Side side{Side::Buy};
        Price limit_px{0};
        Quantity qty{0};
        std::string reason{};
    };

    class MultiVenueRouter
    {
    public:
        bool update(const NormalizedMarketData& md);

        std::optional<VenueTopOfBook> best_ask(SymbolId symbol_id) const;
        std::optional<VenueTopOfBook> best_bid(SymbolId symbol_id) const;

        RouteDecision route_signal(const Signal& signal) const;

        std::vector<VenueTopOfBook> books() const;

    private:
        std::unordered_map<VenueBookKey, VenueTopOfBook, VenueBookKeyHash> books_;
    };
}