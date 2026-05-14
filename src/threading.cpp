#include "llt/threading.hpp"
#include "llt/logging.hpp"

#include <pthread.h>
#include <sched.h>

#include <cstring>
#include <string>

namespace llt 
{

    bool set_current_thread_name(std::string_view thread_name) 
    {
        // Linux thread names are limited to 16 bytes including null terminator.
        // So only 15 visible characters are safe.
        std::string name{thread_name.substr(0, 15)};

        const int rc = pthread_setname_np(pthread_self(), name.c_str());

        if (rc != 0) 
        {
            log(LogLevel::Warn, "threading", "failed to set thread name");
            return false;
        }

        return true;
    }

    bool pin_current_thread_to_cpu(int cpu_id, std::string_view thread_name) 
    {
        set_current_thread_name(thread_name);

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);

        const int rc = pthread_setaffinity_np(
            pthread_self(),
            sizeof(cpu_set_t),
            &cpuset
        );

        if (rc != 0) 
        {
            log(LogLevel::Warn, "threading", "failed to pin thread to requested CPU core");
            return false;
        }

        log(LogLevel::Info, "threading", "thread pinned to requested CPU core");
        return true;
    }
}