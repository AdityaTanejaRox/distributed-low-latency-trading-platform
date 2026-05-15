#include "llt/kill_switch.hpp"
#include "llt/logging.hpp"

#include <filesystem>
#include <iostream>

using namespace llt;

int main()
{
    start_async_logger("logs/kill_switch_demo.log");

    std::filesystem::create_directories("controls");
    std::filesystem::remove("controls/HALT");

    KillSwitch kill_switch{
        KillSwitchLimits{
            .max_routes_per_run = 3,
            .max_rejects_per_run = 2,
            .max_gateway_disconnects = 1
        },
        "controls/HALT"
    };

    for (int i = 0; i < 5; ++i)
    {
        if (!kill_switch.trading_enabled())
        {
            break;
        }

        kill_switch.on_route();

        std::cout << "order_sent=" << i + 1 << '\n';
    }

    auto state = kill_switch.snapshot();

    std::cout << "trading_enabled=" << state.trading_enabled << '\n';
    std::cout << "reason=" << to_string(state.reason) << '\n';

    stop_async_logger();
    return 0;
}