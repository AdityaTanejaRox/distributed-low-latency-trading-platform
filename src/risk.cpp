#include "llt/risk.hpp"

#include <cstdlib>

namespace llt 
{

    RiskEngine::RiskEngine(RiskLimits limits)
        : limits_(limits) {}

    RejectReason RiskEngine::check(const NewOrder& order) const 
    {
        if (order.qty <= 0 || order.qty > limits_.max_order_qty) // reject zero or excessively large orders
        {
            return RejectReason::RiskLimit;
        }

        const auto notional = order.qty * order.limit_px;
        if (notional > limits_.max_notional) // reject orders that exceed notional limit
        {
            return RejectReason::RiskLimit;
        }

        const auto signed_qty = order.side == Side::Buy ? order.qty : -order.qty;
        const auto projected = position_ + signed_qty;

        if (std::llabs(projected) > limits_.max_position) // reject orders that would exceed position limit
        {
            return RejectReason::RiskLimit;
        }

        return RejectReason::None;
    }

    void RiskEngine::on_fill(const Fill& fill, Side side) 
    {
        position_ += side == Side::Buy ? fill.fill_qty : -fill.fill_qty;
    }

    Quantity RiskEngine::position() const 
    {
        return position_;
    }

}