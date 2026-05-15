#include "llt/replay.hpp"
#include "llt/logging.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace llt
{
    ReplayWriter::ReplayWriter(std::string path)
        : path_(std::move(path))
    {
    }

    bool ReplayWriter::open()
    {
        fd_ = ::open(
            path_.c_str(),
            O_CREAT|O_WRONLY|O_TRUNC,
            0644
        );

        if(fd_ < 0)
        {
            log(
                LogLevel::Error,
                "replay",
                "failed opening replay file"
            );

            return false;
        }

        log(
            LogLevel::Info,
            "replay",
            "opened replay file"
        );

        return true;
    }

    void ReplayWriter::append(
        const Envelope& env,
        Sequence seq,
        std::uint64_t ts_ns
    )
    {
        ReplayEvent e{
            seq,
            ts_ns,
            env
        };

        ::write(
            fd_,
            &e,
            sizeof(e)
        );

        log(
            LogLevel::Info,
            "replay",
            "recorded replay event"
        );
    }

    void ReplayWriter::close()
    {
        if(fd_>=0)
        {
            ::close(fd_);

            log(
                LogLevel::Info,
                "replay",
                "closed replay file"
            );
        }
    }

    ReplayReader::ReplayReader(
        std::string path
    )
        : path_(std::move(path))
    {
    }

    bool ReplayReader::open()
    {
        fd_=
        ::open(
            path_.c_str(),
            O_RDONLY
        );

        return fd_>=0;
    }

    std::optional<ReplayEvent>
    ReplayReader::next()
    {
        ReplayEvent e;

        auto n=
        ::read(
            fd_,
            &e,
            sizeof(e)
        );

        if(
            n!=sizeof(e)
        )
        {
            return {};
        }

        return e;
    }

    void ReplayReader::close()
    {
        if(fd_>=0)
        {
            ::close(fd_);
        }
    }
}