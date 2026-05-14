#include "llt/exchange_gateway.hpp"
#include "llt/time.hpp"

namespace llt 
{

    ExchangeGateway::ExchangeGateway(NodeId node_id)
        : node_id_(node_id) {}

    void ExchangeGateway::set_available(bool available) 
    {
        available_ = available;
    }

    std::optional<Envelope> ExchangeGateway::send_order(const NewOrder& order) 
    {
        Envelope out{};

        if (!available_) // simulate gateway being unavailable
        {
            Reject reject{};
            reject.header.type = MsgType::Reject;
            reject.header.source_node = node_id_;
            reject.header.destination_node = order.header.source_node;
            reject.header.sequence = ++sequence_;
            reject.header.send_ts_ns = now_ns();
            reject.client_order_id = order.client_order_id;
            reject.reason = RejectReason::GatewayUnavailable;

            out.type = MsgType::Reject;
            out.payload.reject = reject;
            return out;
        }

        Ack ack{};
        ack.header.type = MsgType::Ack;
        ack.header.source_node = node_id_;
        ack.header.destination_node = order.header.source_node;
        ack.header.sequence = ++sequence_;
        ack.header.send_ts_ns = now_ns();
        ack.client_order_id = order.client_order_id;
        ack.exchange_order_id = next_exchange_order_id_++;
        ack.state = OrderState::Accepted;

        out.type = MsgType::Ack;
        out.payload.ack = ack;
        return out;
    }
}