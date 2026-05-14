#pragma once

#include "llt/order_book.hpp"
#include "llt/types.hpp"

#include <optional>

namespace llt 
{
    class StrategyEngine 
    {
        public:
            explicit StrategyEngine(NodeId node_id);

            std::optional<Signal> on_market_data(const MarketDataUpdate& update);

        private:
            NodeId node_id_;
            TopOfBook book_;
            Sequence signal_sequence_{0};
    };
}