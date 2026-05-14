#include "llt/order_book.hpp"
#include "llt/time.hpp"

namespace llt 
{

    void TopOfBook::apply(const MarketDataUpdate& update) 
    {
        if (has_value_ && update.exchange_sequence <= last_.exchange_sequence) 
        {
            return; // ignore out-of-order or duplicate updates
        }

        last_ = update;
        has_value_ = true;
    }

    std::optional<MarketDataUpdate> TopOfBook::snapshot() const 
    {
        if (!has_value_) 
        {
            return std::nullopt; // no data yet
        }

        return last_; // return a copy of the last update
    }

    bool TopOfBook::is_stale(TimestampNs stale_after_ns) const 
    {
        if (!has_value_) 
        {
            return true; // consider stale if we have never received data
        }

        return now_ns() - last_.header.recv_ts_ns > stale_after_ns; // stale if last update is too old
    }

    Sequence TopOfBook::last_exchange_sequence() const 
    {
        return has_value_ ? last_.exchange_sequence : 0;
    }

}