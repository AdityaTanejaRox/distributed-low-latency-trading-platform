#include "llt/logging.hpp"
#include "llt/time.hpp"

#include <iostream>
#include <mutex>

namespace llt 
{
    namespace 
    {
        std::mutex log_mutex;

        const char* level_to_string(LogLevel level) {
            switch (level) {
                case LogLevel::Info: return "INFO";
                case LogLevel::Warn: return "WARN";
                case LogLevel::Error: return "ERROR";
            }
            return "UNKNOWN";
        }
    }

    void log(LogLevel level, std::string_view component, std::string_view message) 
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cout
            << "{\"ts_ns\":" << now_ns()
            << ",\"level\":\"" << level_to_string(level)
            << "\",\"component\":\"" << component
            << "\",\"message\":\"" << message
            << "\"}" << std::endl;
    }
}