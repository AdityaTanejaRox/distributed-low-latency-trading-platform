#pragma once

#include "llt/types.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace llt
{
    enum class KillSwitchReason : std::uint8_t
    {
        None = 0,
        ManualOperatorHalt = 1,
        MaxRoutesExceeded = 2,
        MaxRejectsExceeded = 3,
        GatewayDisconnect = 4,
        Unknown = 255
    };

    struct KillSwitchLimits
    {
        std::uint64_t max_routes_per_run{100'000};
        std::uint64_t max_rejects_per_run{100};
        std::uint64_t max_gateway_disconnects{5};
    };

    struct KillSwitchState
    {
        bool trading_enabled{true};
        KillSwitchReason reason{KillSwitchReason::None};
        TimestampNs triggered_ts_ns{0};
        std::uint64_t routes_seen{0};
        std::uint64_t rejects_seen{0};
        std::uint64_t gateway_disconnects{0};
    };

    class KillSwitch
    {
    public:
        explicit KillSwitch(
            KillSwitchLimits limits = {},
            std::string manual_halt_file = "controls/HALT"
        );

        bool trading_enabled();

        void on_route();
        void on_reject();
        void on_gateway_disconnect();

        void trigger(KillSwitchReason reason);
        void reset();

        KillSwitchState snapshot() const;

    private:
        std::optional<KillSwitchReason> check_manual_halt_file() const;

        KillSwitchLimits limits_{};
        std::string manual_halt_file_;

        std::atomic<bool> trading_enabled_{true};
        std::atomic<std::uint64_t> reason_{static_cast<std::uint64_t>(KillSwitchReason::None)};
        std::atomic<TimestampNs> triggered_ts_ns_{0};

        std::atomic<std::uint64_t> routes_seen_{0};
        std::atomic<std::uint64_t> rejects_seen_{0};
        std::atomic<std::uint64_t> gateway_disconnects_{0};
    };

    const char* to_string(KillSwitchReason reason);
}