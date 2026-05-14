#include "llt/time.hpp"

#include <chrono>

namespace llt 
{

    TimestampNs now_ns() 
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<TimestampNs>
        (
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()
        );
    }

    LatencyTimer::LatencyTimer()
        : start_ns_(now_ns()) {}

    TimestampNs LatencyTimer::elapsed_ns() const 
    {
        return now_ns() - start_ns_;
    }

}