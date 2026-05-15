#include "llt/replay.hpp"
#include "llt/logging.hpp"
#include "llt/metrics.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>

namespace llt
{
    namespace
    {
        bool write_exact(int fd, const void* data, std::size_t len)
        {
            const char* ptr = static_cast<const char*>(data);
            std::size_t written = 0;

            while (written < len)
            {
                const ssize_t n = ::write(fd, ptr + written, len - written);

                if (n <= 0)
                {
                    return false;
                }

                written += static_cast<std::size_t>(n);
            }

            return true;
        }

        bool read_exact(int fd, void* data, std::size_t len)
        {
            char* ptr = static_cast<char*>(data);
            std::size_t total = 0;

            while (total < len)
            {
                const ssize_t n = ::read(fd, ptr + total, len - total);

                if (n <= 0)
                {
                    return false;
                }

                total += static_cast<std::size_t>(n);
            }

            return true;
        }
    }

    const char* to_string(NodeRole role)
    {
        switch (role)
        {
            case NodeRole::MarketData: return "MarketData";
            case NodeRole::Strategy: return "Strategy";
            case NodeRole::OMS: return "OMS";
            case NodeRole::Gateway: return "Gateway";
            case NodeRole::Simulator: return "Simulator";
        }

        return "Unknown";
    }

    std::uint32_t replay_checksum(const void* data, std::size_t len)
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

    ReplayWriter::ReplayWriter(
        std::string path,
        ReplayWriteMode mode,
        NodeRole node_role
    )
        : path_(std::move(path)),
          mode_(mode),
          node_role_(node_role)
    {
    }

    bool ReplayWriter::open()
    {
        const auto parent = std::filesystem::path(path_).parent_path();

        if (!parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        int flags = O_CREAT | O_WRONLY;

        if (mode_ == ReplayWriteMode::Truncate)
        {
            flags |= O_TRUNC;
        }
        else
        {
            flags |= O_APPEND;
        }

        fd_ = ::open(path_.c_str(), flags, 0644);

        if (fd_ < 0)
        {
            log(LogLevel::Error, "replay", "failed to open replay writer");
            return false;
        }

        log(LogLevel::Info, "replay", "opened replay writer");
        return true;
    }

    bool ReplayWriter::append(
        const Envelope& env,
        Sequence sequence,
        TimestampNs timestamp_ns
    )
    {
        ReplayEvent event{};

        event.header.magic = REPLAY_MAGIC;
        event.header.version = REPLAY_VERSION;
        event.header.header_size = sizeof(ReplayRecordHeader);
        event.header.node_role = node_role_;
        event.header.sequence = sequence;
        event.header.timestamp_ns = timestamp_ns;
        event.header.message_type = env.type;
        event.header.payload_size = sizeof(Envelope);
        event.header.checksum = replay_checksum(&env, sizeof(Envelope));
        event.envelope = env;

        return append_event(event);
    }

    bool ReplayWriter::append_event(const ReplayEvent& event)
    {
        if (fd_ < 0)
        {
            return false;
        }

        if (!write_exact(fd_, &event.header, sizeof(event.header)))
        {
            log(LogLevel::Error, "replay", "failed to write replay header");
            return false;
        }

        if (!write_exact(fd_, &event.envelope, sizeof(Envelope)))
        {
            log(LogLevel::Error, "replay", "failed to write replay payload");
            return false;
        }

        metric_inc(MetricCounter::ReplayEventsWritten);

        return true;
    }

    void ReplayWriter::close()
    {
        if (fd_ >= 0)
        {
            ::fsync(fd_);
            ::close(fd_);
            fd_ = -1;

            log(LogLevel::Info, "replay", "closed replay writer");
        }
    }

    ReplayReader::ReplayReader(std::string path)
        : path_(std::move(path))
    {
    }

    bool ReplayReader::open()
    {
        fd_ = ::open(path_.c_str(), O_RDONLY);

        if (fd_ < 0)
        {
            log(LogLevel::Error, "replay", "failed to open replay reader");
            return false;
        }

        return true;
    }

    std::optional<ReplayEvent> ReplayReader::next()
    {
        if (fd_ < 0)
        {
            return std::nullopt;
        }

        ReplayEvent event{};

        if (!read_exact(fd_, &event.header, sizeof(event.header)))
        {
            return std::nullopt;
        }

        if (event.header.magic != REPLAY_MAGIC ||
            event.header.version != REPLAY_VERSION ||
            event.header.header_size != sizeof(ReplayRecordHeader) ||
            event.header.payload_size != sizeof(Envelope))
        {
            log(LogLevel::Error, "replay", "invalid replay record header");
            return std::nullopt;
        }

        if (!read_exact(fd_, &event.envelope, sizeof(Envelope)))
        {
            log(LogLevel::Error, "replay", "incomplete replay payload");
            return std::nullopt;
        }

        const auto checksum = replay_checksum(&event.envelope, sizeof(Envelope));

        if (checksum != event.header.checksum)
        {
            log(LogLevel::Error, "replay", "replay checksum mismatch");
            return std::nullopt;
        }

        metric_inc(MetricCounter::ReplayEventsRead);

        return event;
    }

    void ReplayReader::close()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }
}