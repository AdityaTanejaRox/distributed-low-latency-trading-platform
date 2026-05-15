#pragma once

#include "llt/types.hpp"

#include <optional>
#include <string>

namespace llt
{
    struct ReplayEvent
    {
        Sequence seq{};
        TimestampNs timestamp_ns{};
        Envelope envelope{};
    };

    class ReplayWriter
    {
    public:
        explicit ReplayWriter(std::string path);

        bool open();

        void append(
            const Envelope& env,
            Sequence seq,
            TimestampNs ts_ns
        );

        void close();

    private:
        std::string path_;
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
}