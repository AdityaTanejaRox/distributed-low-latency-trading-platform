#pragma once

#include "llt/types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace llt 
{
    // ============================================================
    // TCP Binary Transport
    // ============================================================
    //
    // Phase 2 introduces real process-to-process transport.
    //
    // We do NOT use std::future/std::promise/coroutines here because this is
    // intended to model the low-latency trading data path.
    //
    // The goal is:
    //   - explicit control
    //   - predictable blocking behavior
    //   - fixed-size binary framing
    //   - no hidden task schedulers
    //   - no coroutine frame allocation
    //   - no one-future-per-message overhead
    //
    // Later, coroutines may be useful for cold-path admin APIs,
    // telemetry servers, or async replay readers.
    // ============================================================

    constexpr std::uint32_t TCP_MAGIC = 0x4C4C5431; // "LLT1"
    constexpr std::uint16_t TCP_VERSION = 1;
    constexpr std::size_t TCP_MAX_PAYLOAD_SIZE = 4096;

    #pragma pack(push, 1)
    struct TcpFrameHeader 
    {
        std::uint32_t magic{TCP_MAGIC};
        std::uint16_t version{TCP_VERSION};
        std::uint16_t header_size{sizeof(TcpFrameHeader)};

        MsgType type{MsgType::Unknown};

        std::uint32_t payload_size{0};
        Sequence sequence{0};

        // Cheap integrity check.
        // This is not cryptographic.
        // It only catches malformed/truncated/wrong-frame payloads.
        std::uint32_t checksum{0};
    };
    #pragma pack(pop)

    struct TcpFrame 
    {
        TcpFrameHeader header{};
        Envelope envelope{};
    };

    class TcpConnection 
    {
        public:
            explicit TcpConnection(int fd = -1);
            ~TcpConnection();

            TcpConnection(const TcpConnection&) = delete;
            TcpConnection& operator=(const TcpConnection&) = delete;

            TcpConnection(TcpConnection&& other) noexcept;
            TcpConnection& operator=(TcpConnection&& other) noexcept;

            bool valid() const;
            int fd() const;

            bool send_envelope(const Envelope& envelope, Sequence sequence);
            std::optional<Envelope> recv_envelope();

            void close();

        private:
            int fd_{-1};
    };

    class TcpServer 
    {
        public:
            TcpServer() = default;
            ~TcpServer();

            TcpServer(const TcpServer&) = delete;
            TcpServer& operator=(const TcpServer&) = delete;

            bool listen_on(std::uint16_t port);
            std::optional<TcpConnection> accept_one();

            void close();

        private:
            int listen_fd_{-1};
    };

    class TcpClient 
    {
        public:
            static std::optional<TcpConnection> connect_to(
                const std::string& host,
                std::uint16_t port
            );
    };

    std::uint32_t checksum_bytes(const void* data, std::size_t len);
}