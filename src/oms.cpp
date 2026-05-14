#include "llt/oms.hpp"
#include "llt/time.hpp"

namespace llt 
{

    OrderManager::OrderManager(NodeId node_id, RiskLimits limits)
        : node_id_(node_id),
        risk_(limits) {}

    std::optional<Envelope> OrderManager::on_signal(const Signal& signal) 
    {
        NewOrder order{};
        order.header.type = MsgType::NewOrder;
        order.header.source_node = node_id_;
        order.header.destination_node = 4;
        order.header.sequence = ++sequence_;
        order.header.send_ts_ns = now_ns();
        order.client_order_id = next_client_order_id_++;
        order.symbol_id = signal.symbol_id;
        order.side = signal.side;
        order.limit_px = signal.limit_px;
        order.qty = signal.qty;

        const auto reject_reason = risk_.check(order);

        Envelope out{};

        if (reject_reason != RejectReason::None) 
        {
            Reject reject{};
            reject.header.type = MsgType::Reject;
            reject.header.source_node = node_id_;
            reject.header.destination_node = signal.header.source_node;
            reject.header.sequence = ++sequence_;
            reject.header.send_ts_ns = now_ns();
            reject.client_order_id = order.client_order_id;
            reject.reason = reject_reason;

            out.type = MsgType::Reject;
            out.payload.reject = reject;
            return out;
        }

        live_orders_.insert(order.client_order_id);

        out.type = MsgType::NewOrder;
        out.payload.new_order = order;
        return out;
    }

    std::optional<Envelope> OrderManager::on_gateway_ack(const Ack& ack) 
    {
        Envelope out{};
        out.type = MsgType::Ack;
        out.payload.ack = ack;
        return out;
    }

    std::optional<Envelope> OrderManager::on_gateway_reject(const Reject& reject) 
    {
        live_orders_.erase(reject.client_order_id);

        Envelope out{};
        out.type = MsgType::Reject;
        out.payload.reject = reject;
        return out;
    }
}