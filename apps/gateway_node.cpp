#include "llt/exchange_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/tcp_transport.hpp"
#include "llt/threading.hpp"

using namespace llt;

int main()
{
    start_async_logger("logs/gateway_node.log");
    pin_current_thread_to_cpu(3, "gateway-node");

    log(LogLevel::Info, "gateway_node", "starting distributed gateway node on port 21003");

    TcpServer server;

    if (!server.listen_on(21003)) {
        log(LogLevel::Error, "gateway_node", "failed to listen");
        stop_async_logger();
        return 1;
    }

    auto maybe_conn = server.accept_one();

    if (!maybe_conn) {
        log(LogLevel::Error, "gateway_node", "failed to accept OMS connection");
        stop_async_logger();
        return 1;
    }

    TcpConnection conn = std::move(*maybe_conn);
    ExchangeGateway gateway{4};
    Sequence response_seq = 0;

    while (true) {
        auto maybe_msg = conn.recv_envelope();

        if (!maybe_msg) {
            log(LogLevel::Warn, "gateway_node", "OMS disconnected");
            break;
        }

        if (maybe_msg->type != MsgType::NewOrder) {
            continue;
        }

        auto response = gateway.send_order(maybe_msg->payload.new_order);

        if (response) {
            conn.send_envelope(*response, ++response_seq);
            log(LogLevel::Info, "gateway_node", "processed NewOrder and returned Ack/Reject");
        }
    }

    stop_async_logger();
    return 0;
}