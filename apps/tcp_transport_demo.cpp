#include "llt/exchange_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/tcp_transport.hpp"
#include "llt/threading.hpp"
#include "llt/transport_config.hpp"
#include "llt/time.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <netinet/tcp.h>

using namespace llt;

// ============================================================
// Phase 2 TCP Transport Demo
// ============================================================
//
// This demo proves that internal trading messages can cross a real
// TCP boundary using our binary framed protocol.
//
// Topology:
//
//   OMS-side client thread
//          |
//          | TCP NewOrder frame
//          v
//   Gateway-side server thread
//          |
//          | TCP Ack/Reject frame
//          v
//   OMS-side client thread
//
// This is still a local demo on 127.0.0.1,
// but the important thing is that the transport boundary is real.
// ============================================================

static constexpr std::uint16_t PORT = 19090;
static std::atomic<bool> server_ready{false};
static std::atomic<TimestampNs> last_heartbeat_ns{0};
static std::atomic<bool> connection_alive{true};

Envelope make_test_order() 
{
    NewOrder order{};

    order.header.type = MsgType::NewOrder;
    order.header.source_node = 3;
    order.header.destination_node = 4;
    order.header.sequence = 1;
    order.header.send_ts_ns = now_ns();

    order.client_order_id = 42;
    order.symbol_id = 1;
    order.side = Side::Buy;
    order.limit_px = 10025;
    order.qty = 1;

    Envelope env{};
    env.type = MsgType::NewOrder;
    env.payload.new_order = order;

    return env;
}

void heartbeat_sender_thread(TcpConnection* conn) 
{
    pin_current_thread_to_cpu(1, "tcp-heartbeat");

    Sequence heartbeat_seq = 0;

    while (connection_alive.load(std::memory_order_acquire)) 
    {
        Heartbeat hb{};

        hb.header.type = MsgType::Heartbeat;
        hb.header.sequence = ++heartbeat_seq;
        hb.header.send_ts_ns = now_ns();

        hb.role = NodeRole::OMS;
        hb.last_seen_ns = now_ns();

        Envelope env{};
        env.type = MsgType::Heartbeat;
        env.payload.heartbeat = hb;

        if (!conn->send_envelope(env, heartbeat_seq)) 
        {
            log(LogLevel::Warn, "heartbeat", "failed to send heartbeat");
            connection_alive.store(false, std::memory_order_release);
            break;
        }

        log(LogLevel::Info, "heartbeat", "sent heartbeat");

        std::this_thread::sleep_for(
            std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS)
        );
    }
}

void gateway_server_thread() 
{
    pin_current_thread_to_cpu(3, "tcp-gateway");

    TcpServer server;

    if (!server.listen_on(PORT)) 
    {
        return;
    }

    server_ready.store(true, std::memory_order_release);

    auto maybe_conn = server.accept_one();

    if (!maybe_conn) 
    {
        return;
    }

    TcpConnection conn = std::move(*maybe_conn);
    ExchangeGateway gateway{4};

    while (connection_alive.load(std::memory_order_acquire)) 
    {
        auto maybe_msg = conn.recv_envelope();

        if (!maybe_msg) 
        {
            log(LogLevel::Warn, "tcp_gateway", "connection lost");
            connection_alive.store(false, std::memory_order_release);
            break;
        }

        if (maybe_msg->type == MsgType::Heartbeat) 
        {
            last_heartbeat_ns.store(now_ns(), std::memory_order_release);

            log(LogLevel::Info, "tcp_gateway", "received heartbeat");
            continue;
        }

        if (maybe_msg->type != MsgType::NewOrder) 
        {
            continue;
        }

        log(LogLevel::Info, "tcp_gateway", "received NewOrder over TCP");

        auto maybe_response =
            gateway.send_order(maybe_msg->payload.new_order);

        if (!maybe_response) 
        {
            continue;
        }

        conn.send_envelope(*maybe_response, 1);

        log(LogLevel::Info, "tcp_gateway", "sent Ack/Reject over TCP");
    }
}

void oms_client_thread() 
{
    pin_current_thread_to_cpu(2, "tcp-oms");

    while (!server_ready.load(std::memory_order_acquire)) 
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::optional<TcpConnection> maybe_conn;

    while (!maybe_conn) 
    {
        maybe_conn = TcpClient::connect_to("127.0.0.1", PORT);

        if (!maybe_conn) 
        {
            log(LogLevel::Warn, "tcp_oms", "connect failed; retrying");

            std::this_thread::sleep_for(
                std::chrono::milliseconds(RECONNECT_INTERVAL_MS)
            );
        }
    }

    TcpConnection conn = std::move(*maybe_conn);

    std::thread heartbeat
    {
        heartbeat_sender_thread,
        &conn
    };

    Envelope order = make_test_order();

    if (!conn.send_envelope(order, 1)) 
    {
        log(LogLevel::Error, "tcp_oms", "failed to send NewOrder");
        return;
    }

    log(LogLevel::Info, "tcp_oms", "sent NewOrder over TCP");

    auto maybe_response = conn.recv_envelope();

    if (!maybe_response) 
    {
        log(LogLevel::Error, "tcp_oms", "failed to receive response");
        return;
    }

    if (maybe_response->type == MsgType::Ack) 
    {
        log(LogLevel::Info, "tcp_oms", "received Ack over TCP");
    } 

    else if (maybe_response->type == MsgType::Reject) 
    {
        log(LogLevel::Warn, "tcp_oms", "received Reject over TCP");
    }

    connection_alive.store(false, std::memory_order_release);

    if (heartbeat.joinable()) 
    {
        heartbeat.join();
    }
}

int main() 
{
    start_async_logger("logs/tcp_transport_demo.log");

    log(LogLevel::Info, "tcp_demo", "starting phase 2 TCP transport demo");

    std::thread gateway{gateway_server_thread};
    std::thread oms{oms_client_thread};

    oms.join();
    gateway.join();

    log(LogLevel::Info, "tcp_demo", "phase 2 TCP transport demo complete");

    stop_async_logger();
    return 0;
}