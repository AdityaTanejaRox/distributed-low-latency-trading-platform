#pragma once

#include "llt/types.hpp"

#include <optional>

namespace llt 
{
    class TopOfBook 
    {
        public:
            void apply(const MarketDataUpdate& update);

            std::optional<MarketDataUpdate> snapshot() const;

            bool is_stale(TimestampNs stale_after_ns) const;

            Sequence last_exchange_sequence() const;

        private:
            MarketDataUpdate last_{};
            bool has_value_{false};
    };
}