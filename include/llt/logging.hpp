#pragma once

#include <iostream>
#include <mutex>
#include <string_view>

namespace llt 
{
    enum class LogLevel 
    {
        Info,
        Warn,
        Error
    };

    void log(LogLevel level, std::string_view component, std::string_view message);
}