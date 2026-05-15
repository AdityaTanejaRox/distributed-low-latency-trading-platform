#include "llt/metrics.hpp"
#include "llt/time.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace llt
{
    MetricsRegistry& MetricsRegistry::instance()
    {
        static MetricsRegistry registry;
        return registry;
    }

    const char* to_string(MetricCounter counter)
    {
        switch (counter)
        {
            case MetricCounter::MarketDataReceived: return "market_data_received";
            case MetricCounter::SignalsGenerated: return "signals_generated";
            case MetricCounter::OrdersSent: return "orders_sent";
            case MetricCounter::OrdersRejected: return "orders_rejected";
            case MetricCounter::AcksReceived: return "acks_received";
            case MetricCounter::FillsReceived: return "fills_received";
            case MetricCounter::QueueDrops: return "queue_drops";
            case MetricCounter::HeartbeatMisses: return "heartbeat_misses";
            case MetricCounter::GatewayDisconnects: return "gateway_disconnects";
            case MetricCounter::JournalWrites: return "journal_writes";
            case MetricCounter::ReplayEventsWritten: return "replay_events_written";
            case MetricCounter::ReplayEventsRead: return "replay_events_read";
            default: return "unknown";
        }
    }

    void MetricsRegistry::increment(MetricCounter counter, std::uint64_t value)
    {
        counters_[static_cast<std::size_t>(counter)].fetch_add(
            value,
            std::memory_order_relaxed
        );
    }

    void MetricsRegistry::observe_latency_ns(std::uint64_t latency_ns)
    {
        const auto idx =
            latency_write_index_.fetch_add(1, std::memory_order_relaxed)
            % LATENCY_CAPACITY;

        latency_samples_[idx].store(latency_ns, std::memory_order_relaxed);
    }

    MetricsSnapshot MetricsRegistry::snapshot() const
    {
        MetricsSnapshot snap{};

        for (std::size_t i = 0; i < METRIC_COUNTER_COUNT; ++i)
        {
            snap.counters[i] =
                counters_[i].load(std::memory_order_relaxed);
        }

        const auto written =
            latency_write_index_.load(std::memory_order_relaxed);

        const auto count =
            std::min<std::uint64_t>(written, LATENCY_CAPACITY);

        if (count == 0)
        {
            return snap;
        }

        std::vector<std::uint64_t> values;
        values.reserve(static_cast<std::size_t>(count));

        for (std::uint64_t i = 0; i < count; ++i)
        {
            const auto v =
                latency_samples_[i].load(std::memory_order_relaxed);

            if (v > 0)
            {
                values.push_back(v);
            }
        }

        if (values.empty())
        {
            return snap;
        }

        std::sort(values.begin(), values.end());

        auto percentile = [&](double p)
        {
            const auto idx = static_cast<std::size_t>(
                p * static_cast<double>(values.size() - 1)
            );

            return values[idx];
        };

        snap.latency_count = values.size();
        snap.latency_min_ns = values.front();
        snap.latency_p50_ns = percentile(0.50);
        snap.latency_p99_ns = percentile(0.99);
        snap.latency_p999_ns = percentile(0.999);
        snap.latency_max_ns = values.back();

        return snap;
    }

    void MetricsRegistry::write_jsonl(const std::string& path) const
    {
        const auto parent = std::filesystem::path(path).parent_path();

        if (!parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        const auto snap = snapshot();

        std::ofstream out(path, std::ios::app);

        out << "{\"ts_ns\":" << now_ns();

        for (std::size_t i = 0; i < METRIC_COUNTER_COUNT; ++i)
        {
            out << ",\"" << to_string(static_cast<MetricCounter>(i))
                << "\":" << snap.counters[i];
        }

        out
            << ",\"latency_count\":" << snap.latency_count
            << ",\"latency_min_ns\":" << snap.latency_min_ns
            << ",\"latency_p50_ns\":" << snap.latency_p50_ns
            << ",\"latency_p99_ns\":" << snap.latency_p99_ns
            << ",\"latency_p999_ns\":" << snap.latency_p999_ns
            << ",\"latency_max_ns\":" << snap.latency_max_ns
            << "}\n";
    }
}