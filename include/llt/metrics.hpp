#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace llt
{
    enum class MetricCounter : std::uint8_t
    {
        MarketDataReceived = 0,
        SignalsGenerated,
        OrdersSent,
        OrdersRejected,
        AcksReceived,
        FillsReceived,
        QueueDrops,
        HeartbeatMisses,
        GatewayDisconnects,
        JournalWrites,
        ReplayEventsWritten,
        ReplayEventsRead,
        COUNT
    };

    constexpr std::size_t METRIC_COUNTER_COUNT =
        static_cast<std::size_t>(MetricCounter::COUNT);

    struct MetricsSnapshot
    {
        std::array<std::uint64_t, METRIC_COUNTER_COUNT> counters{};
        std::uint64_t latency_count{0};
        std::uint64_t latency_min_ns{0};
        std::uint64_t latency_p50_ns{0};
        std::uint64_t latency_p99_ns{0};
        std::uint64_t latency_p999_ns{0};
        std::uint64_t latency_max_ns{0};
    };

    class MetricsRegistry
    {
    public:
        static MetricsRegistry& instance();

        void increment(MetricCounter counter, std::uint64_t value = 1);

        void observe_latency_ns(std::uint64_t latency_ns);

        MetricsSnapshot snapshot() const;

        void write_jsonl(const std::string& path) const;

    private:
        MetricsRegistry() = default;

        std::array<std::atomic<std::uint64_t>, METRIC_COUNTER_COUNT> counters_{};

        static constexpr std::size_t LATENCY_CAPACITY = 1 << 16;

        mutable std::atomic<std::uint64_t> latency_write_index_{0};
        std::array<std::atomic<std::uint64_t>, LATENCY_CAPACITY> latency_samples_{};
    };

    const char* to_string(MetricCounter counter);

    inline void metric_inc(MetricCounter counter, std::uint64_t value = 1)
    {
        MetricsRegistry::instance().increment(counter, value);
    }

    inline void metric_latency(std::uint64_t latency_ns)
    {
        MetricsRegistry::instance().observe_latency_ns(latency_ns);
    }
}