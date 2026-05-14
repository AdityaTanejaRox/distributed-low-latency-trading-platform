#include "llt/logging.hpp"
#include "llt/time.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace llt {

namespace {

// Bounded logger queue.
// This avoids unbounded memory growth if logs are produced faster than
// the logger thread can write them.
constexpr std::size_t LOG_QUEUE_CAPACITY = 1 << 15;
constexpr std::size_t COMPONENT_LEN = 32;
constexpr std::size_t MESSAGE_LEN = 192;

struct LogEvent 
{
    TimestampNs ts_ns{0};
    LogLevel level{LogLevel::Info};
    std::array<char, COMPONENT_LEN> component{};
    std::array<char, MESSAGE_LEN> message{};
};

// Simple SPSC-style queue for logging.
// Many producers technically call log(), so this is not a perfect MPSC logger.
// For this project phase, we use an atomic spin claim on the write slot.
// Later, this can become a per-thread SPSC logger or Disruptor-style ring.
struct AsyncLogQueue 
{
    std::array<LogEvent, LOG_QUEUE_CAPACITY> buffer{};
    std::atomic<std::size_t> head{0};
    std::atomic<std::size_t> tail{0};
    std::atomic<std::uint64_t> dropped{0};
};

AsyncLogQueue g_queue;
std::atomic<bool> g_logger_running{false};
std::atomic<bool> g_logger_started{false};
std::thread g_logger_thread;

const char* level_to_string(LogLevel level) 
{
    switch (level) 
    {
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }

    return "UNKNOWN";
}

void copy_truncated(std::string_view src, char* dst, std::size_t dst_size) 
{
    if (dst_size == 0) 
    {
        return;
    }

    const std::size_t n = std::min(src.size(), dst_size - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

bool try_push_log_event(const LogEvent& event) 
{
    while (true) 
    {
        const std::size_t head = g_queue.head.load(std::memory_order_relaxed);
        const std::size_t tail = g_queue.tail.load(std::memory_order_acquire);
        const std::size_t next = (head + 1) % LOG_QUEUE_CAPACITY;

        if (next == tail) 
        {
            g_queue.dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (g_queue.head.compare_exchange_weak(
                const_cast<std::size_t&>(head),
                next,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) 
        {
            g_queue.buffer[head] = event;
            return true;
        }
    }
}

bool try_pop_log_event(LogEvent& out) 
{
    const std::size_t tail = g_queue.tail.load(std::memory_order_relaxed);

    if (tail == g_queue.head.load(std::memory_order_acquire)) 
    {
        return false;
    }

    out = g_queue.buffer[tail];

    const std::size_t next = (tail + 1) % LOG_QUEUE_CAPACITY;
    g_queue.tail.store(next, std::memory_order_release);

    return true;
}

void logger_loop(std::string log_file_path) 
{
    std::filesystem::create_directories(std::filesystem::path(log_file_path).parent_path());

    std::ofstream out(log_file_path, std::ios::app);

    LogEvent event{};

    while (g_logger_running.load(std::memory_order_acquire)) 
    {
        bool did_work = false;

        while (try_pop_log_event(event)) 
        {
            did_work = true;

            out
                << "{\"ts_ns\":" << event.ts_ns
                << ",\"level\":\"" << level_to_string(event.level)
                << "\",\"component\":\"" << event.component.data()
                << "\",\"message\":\"" << event.message.data()
                << "\"}\n";
        }

        if (did_work) 
        {
            out.flush();
        } 
        else 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Drain remaining messages during shutdown.
    while (try_pop_log_event(event)) 
    {
        out
            << "{\"ts_ns\":" << event.ts_ns
            << ",\"level\":\"" << level_to_string(event.level)
            << "\",\"component\":\"" << event.component.data()
            << "\",\"message\":\"" << event.message.data()
            << "\"}\n";
    }

    const auto dropped = g_queue.dropped.load(std::memory_order_relaxed);

    out
        << "{\"ts_ns\":" << now_ns()
        << ",\"level\":\"INFO\""
        << ",\"component\":\"logger\""
        << ",\"message\":\"logger stopped; dropped_logs=" << dropped << "\"}\n";

    out.flush();
}

}

void start_async_logger(std::string_view log_file_path) 
{
    bool expected = false;

    if (!g_logger_started.compare_exchange_strong(expected, true)) 
    {
        return;
    }

    g_logger_running.store(true, std::memory_order_release);
    g_logger_thread = std::thread(logger_loop, std::string(log_file_path));
}

void stop_async_logger() 
{
    if (!g_logger_started.load(std::memory_order_acquire)) 
    {
        return;
    }

    g_logger_running.store(false, std::memory_order_release);

    if (g_logger_thread.joinable()) 
    {
        g_logger_thread.join();
    }
}

void log(LogLevel level, std::string_view component, std::string_view message) 
{
    LogEvent event{};
    event.ts_ns = now_ns();
    event.level = level;

    copy_truncated(component, event.component.data(), event.component.size());
    copy_truncated(message, event.message.data(), event.message.size());

    try_push_log_event(event);
}

}