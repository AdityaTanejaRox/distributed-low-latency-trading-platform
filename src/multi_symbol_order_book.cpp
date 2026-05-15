#include "llt/multi_symbol_order_book.hpp"

namespace llt
{
    bool MultiSymbolTopOfBook::apply(const MarketDataUpdate& update)
    {
        if (update.symbol_id == 0)
        {
            return false;
        }

        if (update.bid_px <= 0 || update.ask_px <= 0)
        {
            return false;
        }

        if (update.bid_px > update.ask_px)
        {
            return false;
        }

        auto& book = books_[update.symbol_id];

        book.symbol_id = update.symbol_id;
        book.symbol = update.symbol;
        book.bid_px = update.bid_px;
        book.bid_qty = update.bid_qty;
        book.ask_px = update.ask_px;
        book.ask_qty = update.ask_qty;
        book.exchange_sequence = update.exchange_sequence;
        book.recv_ts_ns = update.header.recv_ts_ns;

        return true;
    }

    std::optional<SymbolBookSnapshot>
    MultiSymbolTopOfBook::snapshot(SymbolId symbol_id) const
    {
        const auto it = books_.find(symbol_id);

        if (it == books_.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    std::vector<SymbolBookSnapshot>
    MultiSymbolTopOfBook::snapshots() const
    {
        std::vector<SymbolBookSnapshot> out;
        out.reserve(books_.size());

        for (const auto& [_, snapshot] : books_)
        {
            out.push_back(snapshot);
        }

        return out;
    }

    std::size_t MultiSymbolTopOfBook::symbol_count() const
    {
        return books_.size();
    }

    void MultiSymbolTopOfBook::clear()
    {
        books_.clear();
    }
}