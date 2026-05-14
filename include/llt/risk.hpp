#pragma once

#include "llt/types.hpp"

namespace llt 
{
    struct RiskLimits 
    {
        Quantity max_position{100};
        Quantity max_order_qty{10};
        Price max_notional{1'000'000};
    };

    class RiskEngine 
    {
        public:
            explicit RiskEngine(RiskLimits limits);

            RejectReason check(const NewOrder& order) const;

            void on_fill(const Fill& fill, Side side);

            Quantity position() const;

        private:
            RiskLimits limits_;
            Quantity position_{0};
    };
}