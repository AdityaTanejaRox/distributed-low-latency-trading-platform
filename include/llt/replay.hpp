#pragma once

#include "llt/types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace llt
{
    constexpr std::uint32_t REPLAY_MAGIC = 0x52504C31;
    constexpr std::uint16_t REPLAY_VERSION = 2;

    enum class ReplayWriteMode
    {
        Truncate,
        Append
    };

#pragma pack(push, 1)
    struct ReplayRecordHeader
    {
        std::uint32_t magic{REPLAY_MAGIC};
        std::uint16_t version{REPLAY_VERSION};
        std::uint16_t header_size{sizeof(ReplayRecordHeader)};

        NodeRole node_role{NodeRole::Simulator};
        Sequence sequence{0};
        TimestampNs timestamp_ns{0};

        MsgType message_type{MsgType::Unknown};

        std::uint32_t payload_size{sizeof(Envelope)};
        std::uint32_t checksum{0};
    };
#pragma pack(pop)

    struct ReplayEvent
    {
        ReplayRecordHeader header{};
        Envelope envelope{};
    };

    struct ReplayStats
    {
        std::uint64_t events_read{0};
        std::uint64_t market_data_events{0};
        std::uint64_t signals_generated{0};
        std::uint64_t orders_generated{0};
        std::uint64_t acks_generated{0};
        std::uint64_t rejects_generated{0};
        std::uint64_t corrupt_records{0};
    };

    class ReplayWriter
    {
    public:
        ReplayWriter(
            std::string path,
            ReplayWriteMode mode = ReplayWriteMode::Append,
            NodeRole node_role = NodeRole::Simulator
        );

        bool open();

        bool append(
            const Envelope& env,
            Sequence sequence,
            TimestampNs timestamp_ns
        );

        bool append_event(const ReplayEvent& event);

        void close();

    private:
        std::string path_;
        ReplayWriteMode mode_{ReplayWriteMode::Append};
        NodeRole node_role_{NodeRole::Simulator};
        int fd_{-1};
    };

    class ReplayReader
    {
    public:
        explicit ReplayReader(std::string path);

        bool open();
        std::optional<ReplayEvent> next();
        void close();

    private:
        std::string path_;
        int fd_{-1};
    };

    std::uint32_t replay_checksum(const void* data, std::size_t len);
    const char* to_string(NodeRole role);
}