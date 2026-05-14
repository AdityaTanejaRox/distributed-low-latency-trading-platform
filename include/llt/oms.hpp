#pragma once

#include "llt/risk.hpp"
#include "llt/types.hpp"

#include <optional>
#include <unordered_set>

namespace llt 
{
    class OrderManager 
    {
        public:
            OrderManager(NodeId node_id, RiskLimits limits);

            std::optional<Envelope> on_signal(const Signal& signal);

            std::optional<Envelope> on_gateway_ack(const Ack& ack);

            std::optional<Envelope> on_gateway_reject(const Reject& reject);

        private:
            NodeId node_id_;
            RiskEngine risk_;
            ClientOrderId next_client_order_id_{1};
            Sequence sequence_{0};
            std::unordered_set<ClientOrderId> live_orders_;
    };
}