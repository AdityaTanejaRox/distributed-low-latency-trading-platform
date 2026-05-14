#pragma once

#include "llt/types.hpp"

namespace llt 
{

    TimestampNs now_ns();

    class LatencyTimer {
        public:
            LatencyTimer();

            TimestampNs elapsed_ns() const;

        private:
            TimestampNs start_ns_;
    };

}