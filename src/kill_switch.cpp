// #include "llt/kill_switch.hpp"
// #include "llt/logging.hpp"
// #include "llt/time.hpp"

// #include <filesystem>

// namespace llt
// {
//     const char* to_string(KillSwitchReason reason)
//     {
//         switch (reason)
//         {
//             case KillSwitchReason::None: return "None";
//             case KillSwitchReason::ManualOperatorHalt: return "ManualOperatorHalt";
//             case KillSwitchReason::MaxRejectsExceeded: return "MaxRejectsExceeded";
//             case KillSwitchReason::MaxOrdersExceeded: return "MaxOrdersExceeded";
//             case KillSwitchReason::GatewayDisconnect: return "GatewayDisconnect";
//             case KillSwitchReason::StaleMarketData: return "StaleMarketData";
//             default: return "Unknown";
//         }
//     }

//     KillSwitch::KillSwitch(
//         KillSwitchLimits limits,
//         std::string manual_halt_file
//     )
//         : limits_(limits),
//           manual_halt_file_(std::move(manual_halt_file))
//     {
//     }

//     std::optional<KillSwitchReason> KillSwitch::check_manual_halt_file() const
//     {
//         if (std::filesystem::exists(manual_halt_file_))
//         {
//             return KillSwitchReason::ManualOperatorHalt;
//         }

//         return std::nullopt;
//     }

//     bool KillSwitch::trading_enabled()
//     {
//         if (auto reason = check_manual_halt_file())
//         {
//             trigger(*reason);
//         }

//         return trading_enabled_.load(std::memory_order_acquire);
//     }

//     void KillSwitch::trigger(KillSwitchReason reason)
//     {
//         bool expected = true;

//         if (trading_enabled_.compare_exchange_strong(
//                 expected,
//                 false,
//                 std::memory_order_acq_rel
//             ))
//         {
//             reason_.store(
//                 static_cast<std::uint64_t>(reason),
//                 std::memory_order_release
//             );

//             triggered_ts_ns_.store(now_ns(), std::memory_order_release);

//             log(LogLevel::Error, "kill_switch", to_string(reason));
//         }
//     }

//     void KillSwitch::reset()
//     {
//         orders_sent_.store(0, std::memory_order_release);
//         rejects_seen_.store(0, std::memory_order_release);
//         gateway_disconnects_.store(0, std::memory_order_release);
//         reason_.store(static_cast<std::uint64_t>(KillSwitchReason::None), std::memory_order_release);
//         triggered_ts_ns_.store(0, std::memory_order_release);
//         trading_enabled_.store(true, std::memory_order_release);

//         log(LogLevel::Warn, "kill_switch", "kill switch reset");
//     }

//     void KillSwitch::on_order_sent()
//     {
//         const auto count =
//             orders_sent_.fetch_add(1, std::memory_order_acq_rel) + 1;

//         if (count > limits_.max_orders_per_run)
//         {
//             trigger(KillSwitchReason::MaxOrdersExceeded);
//         }
//     }

//     void KillSwitch::on_reject()
//     {
//         const auto count =
//             rejects_seen_.fetch_add(1, std::memory_order_acq_rel) + 1;

//         if (count > limits_.max_rejects_per_run)
//         {
//             trigger(KillSwitchReason::MaxRejectsExceeded);
//         }
//     }

//     void KillSwitch::on_gateway_disconnect()
//     {
//         const auto count =
//             gateway_disconnects_.fetch_add(1, std::memory_order_acq_rel) + 1;

//         if (count > limits_.max_gateway_disconnects)
//         {
//             trigger(KillSwitchReason::GatewayDisconnect);
//         }
//     }

//     KillSwitchState KillSwitch::snapshot() const
//     {
//         return KillSwitchState{
//             .trading_enabled = trading_enabled_.load(std::memory_order_acquire),
//             .reason = static_cast<KillSwitchReason>(
//                 reason_.load(std::memory_order_acquire)
//             ),
//             .triggered_ts_ns = triggered_ts_ns_.load(std::memory_order_acquire),
//             .orders_sent = orders_sent_.load(std::memory_order_acquire),
//             .rejects_seen = rejects_seen_.load(std::memory_order_acquire),
//             .gateway_disconnects = gateway_disconnects_.load(std::memory_order_acquire)
//         };
//     }
// }

#include "llt/kill_switch.hpp"
#include "llt/logging.hpp"
#include "llt/time.hpp"

#include <filesystem>

namespace llt
{
    const char* to_string(KillSwitchReason reason)
    {
        switch (reason)
        {
            case KillSwitchReason::None: return "None";
            case KillSwitchReason::ManualOperatorHalt: return "ManualOperatorHalt";
            case KillSwitchReason::MaxRoutesExceeded: return "MaxRoutesExceeded";
            case KillSwitchReason::MaxRejectsExceeded: return "MaxRejectsExceeded";
            case KillSwitchReason::GatewayDisconnect: return "GatewayDisconnect";
            default: return "Unknown";
        }
    }

    KillSwitch::KillSwitch(KillSwitchLimits limits, std::string manual_halt_file)
        : limits_(limits),
          manual_halt_file_(std::move(manual_halt_file))
    {
    }

    std::optional<KillSwitchReason> KillSwitch::check_manual_halt_file() const
    {
        if (std::filesystem::exists(manual_halt_file_))
        {
            return KillSwitchReason::ManualOperatorHalt;
        }

        return std::nullopt;
    }

    bool KillSwitch::trading_enabled()
    {
        if (auto reason = check_manual_halt_file())
        {
            trigger(*reason);
        }

        return trading_enabled_.load(std::memory_order_acquire);
    }

    void KillSwitch::trigger(KillSwitchReason reason)
    {
        bool expected = true;

        if (trading_enabled_.compare_exchange_strong(expected, false))
        {
            reason_.store(static_cast<std::uint64_t>(reason));
            triggered_ts_ns_.store(now_ns());
            log(LogLevel::Error, "kill_switch", to_string(reason));
        }
    }

    void KillSwitch::reset()
    {
        routes_seen_.store(0);
        rejects_seen_.store(0);
        gateway_disconnects_.store(0);
        reason_.store(static_cast<std::uint64_t>(KillSwitchReason::None));
        triggered_ts_ns_.store(0);
        trading_enabled_.store(true);

        log(LogLevel::Warn, "kill_switch", "kill switch reset");
    }

    void KillSwitch::on_route()
    {
        const auto count = routes_seen_.fetch_add(1) + 1;

        if (count > limits_.max_routes_per_run)
        {
            trigger(KillSwitchReason::MaxRoutesExceeded);
        }
    }

    void KillSwitch::on_reject()
    {
        const auto count = rejects_seen_.fetch_add(1) + 1;

        if (count > limits_.max_rejects_per_run)
        {
            trigger(KillSwitchReason::MaxRejectsExceeded);
        }
    }

    void KillSwitch::on_gateway_disconnect()
    {
        const auto count = gateway_disconnects_.fetch_add(1) + 1;

        if (count > limits_.max_gateway_disconnects)
        {
            trigger(KillSwitchReason::GatewayDisconnect);
        }
    }

    KillSwitchState KillSwitch::snapshot() const
    {
        return KillSwitchState{
            .trading_enabled = trading_enabled_.load(),
            .reason = static_cast<KillSwitchReason>(reason_.load()),
            .triggered_ts_ns = triggered_ts_ns_.load(),
            .routes_seen = routes_seen_.load(),
            .rejects_seen = rejects_seen_.load(),
            .gateway_disconnects = gateway_disconnects_.load()
        };
    }
}