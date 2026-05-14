#pragma once

#include <string_view>

namespace llt 
{

    // Pins the current thread to a specific Linux CPU core.
    // This is important in low-latency systems because it reduces scheduler
    // migration, cache coldness, and unpredictable tail latency.
    bool pin_current_thread_to_cpu(int cpu_id, std::string_view thread_name);

    // Gives the current thread a readable name visible in tools like htop,
    // perf top, ps -L, and /proc/<pid>/task/<tid>/comm.
    bool set_current_thread_name(std::string_view thread_name);

}