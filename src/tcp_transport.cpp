#include "llt/tcp_transport.hpp"
#include "llt/logging.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/tcp.h>

#include <cerrno>
#include <cstring>

namespace llt 
{

    namespace 
    {

        // ============================================================
        // send_exact
        // ============================================================
        //
        // TCP is a byte stream, not a message protocol.
        //
        // send(fd, buffer, 128, 0) is not guaranteed to send 128 bytes.
        // It may send fewer bytes.
        //
        // Therefore every binary protocol needs send_exact / recv_exact helpers.
        //
        // This function loops until the full buffer is written or an error occurs.
        // ============================================================

        bool send_exact(int fd, const void* data, std::size_t len) 
        {
            const char* ptr = static_cast<const char*>(data);
            std::size_t sent = 0;

            while (sent < len) 
            {
                const ssize_t n = ::send(fd, ptr + sent, len - sent, MSG_NOSIGNAL);

                if (n <= 0) 
                {
                    return false;
                }

                sent += static_cast<std::size_t>(n);
            }

            return true;
        }

        // ============================================================
        // recv_exact
        // ============================================================
        //
        // TCP recv() can also return partial data.
        //
        // If the frame header is 32 bytes, recv() may return 7 bytes first,
        // then 25 bytes later.
        //
        // recv_exact forces deterministic framing behavior.
        // ============================================================

        bool recv_exact(int fd, void* data, std::size_t len) 
        {
            char* ptr = static_cast<char*>(data);
            std::size_t received = 0;

            while (received < len) 
            {
                const ssize_t n = ::recv(fd, ptr + received, len - received, 0);

                if (n <= 0) 
                {
                    return false;
                }

                received += static_cast<std::size_t>(n);
            }

            return true;
        }

        std::uint32_t payload_size_for_type(MsgType type) 
        {
            switch (type) 
            {
                case MsgType::MarketData: return sizeof(MarketDataUpdate);
                case MsgType::Signal: return sizeof(Signal);
                case MsgType::NewOrder: return sizeof(NewOrder);
                case MsgType::Ack: return sizeof(Ack);
                case MsgType::Fill: return sizeof(Fill);
                case MsgType::Reject: return sizeof(Reject);
                case MsgType::Heartbeat: return sizeof(Heartbeat);
                case MsgType::RiskState: return sizeof(RiskState);
                default: return 0;
            }
        }

        const void* payload_ptr(const Envelope& env) 
        {
            switch (env.type) 
            {
                case MsgType::MarketData: return &env.payload.market_data;
                case MsgType::Signal: return &env.payload.signal;
                case MsgType::NewOrder: return &env.payload.new_order;
                case MsgType::Ack: return &env.payload.ack;
                case MsgType::Fill: return &env.payload.fill;
                case MsgType::Reject: return &env.payload.reject;
                case MsgType::Heartbeat: return &env.payload.heartbeat;
                case MsgType::RiskState: return &env.payload.risk_state;
                default: return nullptr;
            }
        }

        void* mutable_payload_ptr(Envelope& env, MsgType type) {
            switch (type) 
            {
                case MsgType::MarketData: return &env.payload.market_data;
                case MsgType::Signal: return &env.payload.signal;
                case MsgType::NewOrder: return &env.payload.new_order;
                case MsgType::Ack: return &env.payload.ack;
                case MsgType::Fill: return &env.payload.fill;
                case MsgType::Reject: return &env.payload.reject;
                case MsgType::Heartbeat: return &env.payload.heartbeat;
                case MsgType::RiskState: return &env.payload.risk_state;
                default: return nullptr;
            }
        }

        } // namespace

        std::uint32_t checksum_bytes(const void* data, std::size_t len) 
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);

            std::uint32_t hash = 2166136261u;

            for (std::size_t i = 0; i < len; ++i) 
            {
                hash ^= bytes[i];
                hash *= 16777619u;
            }

            return hash;
        }

        TcpConnection::TcpConnection(int fd)
            : fd_(fd) {}

        TcpConnection::~TcpConnection() 
        {
            close();
        }

        TcpConnection::TcpConnection(TcpConnection&& other) noexcept
            : fd_(other.fd_) 
        {
            other.fd_ = -1;
        }

        TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept 
        {
            if (this != &other) 
            {
                close();
                fd_ = other.fd_;
                other.fd_ = -1;
            }

            return *this;
        }

        bool TcpConnection::valid() const 
        {
            return fd_ >= 0;
        }

        int TcpConnection::fd() const 
        {
            return fd_;
        }

        bool TcpConnection::send_envelope(const Envelope& envelope, Sequence sequence) 
        {
            if (!valid()) 
            {
                return false;
            }

            const std::uint32_t payload_size = payload_size_for_type(envelope.type);
            const void* payload = payload_ptr(envelope);

            if (payload == nullptr || payload_size == 0) 
            {
                log(LogLevel::Warn, "tcp_transport", "attempted to send invalid envelope");
                return false;
            }

            TcpFrameHeader header{};
            header.magic = TCP_MAGIC;
            header.version = TCP_VERSION;
            header.header_size = sizeof(TcpFrameHeader);
            header.type = envelope.type;
            header.payload_size = payload_size;
            header.sequence = sequence;
            header.checksum = checksum_bytes(payload, payload_size);

            // ============================================================
            // Backpressure Policy Hook
            // ============================================================
            //
            // In future phases this transport layer will track:
            //
            //   - outbound queue depth
            //   - socket write pressure
            //   - send retry counts
            //   - retransmission pressure
            //
            // Policy options:
            //
            // NORMAL
            //   queue healthy
            //
            // THROTTLE
            //   reduce producer rate
            //
            // FAIL_CLOSED
            //   reject/drop traffic
            //
            // We intentionally prefer:
            //
            //   deterministic rejection
            //
            // over:
            //
            //   hidden latency growth
            //
            // ============================================================

            if (!send_exact(fd_, &header, sizeof(header))) 
            {
                log(LogLevel::Warn, "tcp_transport", "failed to send TCP frame header");
                return false;
            }

            if (!send_exact(fd_, payload, payload_size)) 
            {
                log(LogLevel::Warn, "tcp_transport", "failed to send TCP frame payload");
                return false;
            }

            return true;
        }

        std::optional<Envelope> TcpConnection::recv_envelope() 
        {
            if (!valid()) 
            {
                return std::nullopt;
            }

            TcpFrameHeader header{};

            if (!recv_exact(fd_, &header, sizeof(header))) 
            {
                return std::nullopt;
            }

            if (header.magic != TCP_MAGIC || header.version != TCP_VERSION) 
            {
                log(LogLevel::Warn, "tcp_transport", "received invalid frame magic/version");
                return std::nullopt;
            }

            if (header.payload_size > TCP_MAX_PAYLOAD_SIZE) 
            {
                log(LogLevel::Warn, "tcp_transport", "received oversized payload");
                return std::nullopt;
            }

            const std::uint32_t expected_size = payload_size_for_type(header.type);

            if (expected_size == 0 || expected_size != header.payload_size) 
            {
                log(LogLevel::Warn, "tcp_transport", "received unexpected payload size");
                return std::nullopt;
            }

            Envelope env{};
            env.type = header.type;

            void* payload = mutable_payload_ptr(env, header.type);

            if (payload == nullptr) 
            {
                return std::nullopt;
            }

            if (!recv_exact(fd_, payload, header.payload_size)) 
            {
                return std::nullopt;
            }

            const std::uint32_t actual_checksum =
                checksum_bytes(payload, header.payload_size);

            if (actual_checksum != header.checksum) 
            {
                log(LogLevel::Warn, "tcp_transport", "payload checksum mismatch");
                return std::nullopt;
            }

            return env;
        }

        void TcpConnection::close() 
        {
            if (fd_ >= 0) 
            {
                ::close(fd_);
                fd_ = -1;
            }
        }

        TcpServer::~TcpServer() 
        {
            close();
        }

        bool TcpServer::listen_on(std::uint16_t port) 
        {
            listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);

            if (listen_fd_ < 0) 
            {
                log(LogLevel::Error, "tcp_server", "failed to create socket");
                return false;
            }

            int yes = 1;

            ::setsockopt(
                listen_fd_,
                SOL_SOCKET,
                SO_REUSEADDR,
                &yes,
                sizeof(yes)
            );

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port);

            if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) 
            {
                log(LogLevel::Error, "tcp_server", "failed to bind socket");
                close();
                return false;
            }

            if (::listen(listen_fd_, 16) < 0) 
            {
                log(LogLevel::Error, "tcp_server", "failed to listen");
                close();
                return false;
            }

            log(LogLevel::Info, "tcp_server", "listening on loopback TCP port");
            return true;
        }

        std::optional<TcpConnection> TcpServer::accept_one() 
        {
            if (listen_fd_ < 0) 
            {
                return std::nullopt;
            }

            sockaddr_in peer{};
            socklen_t peer_len = sizeof(peer);

            const int fd = ::accept(
                listen_fd_,
                reinterpret_cast<sockaddr*>(&peer),
                &peer_len
            );

            if (fd < 0) 
            {
                log(LogLevel::Warn, "tcp_server", "accept failed");
                return std::nullopt;
            }

            log(LogLevel::Info, "tcp_server", "accepted TCP client");
            return TcpConnection{fd};
        }

        void TcpServer::close() 
        {
            if (listen_fd_ >= 0) 
            {
                ::close(listen_fd_);
                listen_fd_ = -1;
            }
        }

        std::optional<TcpConnection> TcpClient::connect_to(
            const std::string& host,
            std::uint16_t port
        ) {
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

            if (fd < 0) 
            {
                log(LogLevel::Error, "tcp_client", "failed to create socket");
                return std::nullopt;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            int flag = 1;

            ::setsockopt(
                fd,
                IPPROTO_TCP,
                TCP_NODELAY,
                &flag,
                sizeof(flag)
            );

            if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) 
            {
                ::close(fd);
                log(LogLevel::Error, "tcp_client", "invalid host address");
                return std::nullopt;
            }

            if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) 
            {
                ::close(fd);
                log(LogLevel::Error, "tcp_client", "connect failed");
                return std::nullopt;
            }

            log(LogLevel::Info, "tcp_client", "connected to TCP server");
            return TcpConnection{fd};
    }
}