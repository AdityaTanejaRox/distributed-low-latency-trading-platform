#pragma once

#include <string_view>

namespace llt 
{
    enum class LogLevel 
    {
        Info,
        Warn,
        Error
    };

    // Starts the async logger thread.
    // Creates the logs/ directory if needed.
    void start_async_logger(std::string_view log_file_path = "logs/runtime.log");

    // Flushes remaining log messages and stops the logger thread.
    void stop_async_logger();

    // Non-blocking hot-path log call.
    // This only attempts to enqueue a compact log event.
    // If the queue is full, the log message is dropped.
    void log(LogLevel level, std::string_view component, std::string_view message);
}