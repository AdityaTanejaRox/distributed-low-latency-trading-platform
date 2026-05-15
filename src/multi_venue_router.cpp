#include "llt/multi_venue_router.hpp"

namespace llt
{
    bool MultiVenueRouter::update(const NormalizedMarketData& md)
    {
        const auto& u = md.update;

        if (u.symbol_id == 0)
        {
            return false;
        }

        if (u.bid_px <= 0 || u.ask_px <= 0 || u.bid_px > u.ask_px)
        {
            return false;
        }

        VenueBookKey key{
            .venue = md.venue,
            .symbol_id = u.symbol_id
        };

        books_[key] = VenueTopOfBook{
            .venue = md.venue,
            .symbol_id = u.symbol_id,
            .symbol = u.symbol,
            .bid_px = u.bid_px,
            .bid_qty = u.bid_qty,
            .ask_px = u.ask_px,
            .ask_qty = u.ask_qty,
            .exchange_sequence = u.exchange_sequence,
            .recv_ts_ns = u.header.recv_ts_ns
        };

        return true;
    }

    std::optional<VenueTopOfBook>
    MultiVenueRouter::best_ask(SymbolId symbol_id) const
    {
        std::optional<VenueTopOfBook> best;

        for (const auto& [_, book] : books_)
        {
            if (book.symbol_id != symbol_id)
            {
                continue;
            }

            if (!best || book.ask_px < best->ask_px)
            {
                best = book;
            }
        }

        return best;
    }

    std::optional<VenueTopOfBook>
    MultiVenueRouter::best_bid(SymbolId symbol_id) const
    {
        std::optional<VenueTopOfBook> best;

        for (const auto& [_, book] : books_)
        {
            if (book.symbol_id != symbol_id)
            {
                continue;
            }

            if (!best || book.bid_px > best->bid_px)
            {
                best = book;
            }
        }

        return best;
    }

    RouteDecision MultiVenueRouter::route_signal(const Signal& signal) const
    {
        if (signal.side == Side::Buy)
        {
            auto venue = best_ask(signal.symbol_id);

            if (!venue)
            {
                return RouteDecision{
                    .routeable = false,
                    .symbol_id = signal.symbol_id,
                    .side = signal.side,
                    .qty = signal.qty,
                    .reason = "no ask venue available"
                };
            }

            return RouteDecision{
                .routeable = true,
                .venue = venue->venue,
                .symbol_id = signal.symbol_id,
                .symbol = venue->symbol,
                .side = signal.side,
                .limit_px = venue->ask_px,
                .qty = signal.qty,
                .reason = "routed to lowest live ask"
            };
        }

        auto venue = best_bid(signal.symbol_id);

        if (!venue)
        {
            return RouteDecision{
                .routeable = false,
                .symbol_id = signal.symbol_id,
                .side = signal.side,
                .qty = signal.qty,
                .reason = "no bid venue available"
            };
        }

        return RouteDecision{
            .routeable = true,
            .venue = venue->venue,
            .symbol_id = signal.symbol_id,
            .symbol = venue->symbol,
            .side = signal.side,
            .limit_px = venue->bid_px,
            .qty = signal.qty,
            .reason = "routed to highest live bid"
        };
    }

    std::vector<VenueTopOfBook> MultiVenueRouter::books() const
    {
        std::vector<VenueTopOfBook> out;
        out.reserve(books_.size());

        for (const auto& [_, book] : books_)
        {
            out.push_back(book);
        }

        return out;
    }
}