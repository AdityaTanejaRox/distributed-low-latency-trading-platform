#pragma once

#include "llt/types.hpp"

#include <optional>

namespace llt 
{
    class ExchangeGateway 
    {
        public:
            explicit ExchangeGateway(NodeId node_id);

            std::optional<Envelope> send_order(const NewOrder& order);

            void set_available(bool available);

        private:
            NodeId node_id_;
            bool available_{true};
            OrderId next_exchange_order_id_{1000};
            Sequence sequence_{0};
    };
}