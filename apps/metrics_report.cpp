#include "llt/logging.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    llt::start_async_logger("logs/metrics_report.log");

    const std::vector<std::string> paths{
        "metrics/market_data_node.jsonl",
        "metrics/strategy_node.jsonl",
        "metrics/oms_node.jsonl",
        "metrics/gateway_node.jsonl"
    };

    for (const auto& path : paths)
    {
        std::cout << "\n== " << path << " ==\n";

        if (!std::filesystem::exists(path))
        {
            std::cout << "missing\n";
            continue;
        }

        std::ifstream in(path);
        std::string line;
        std::string last;

        while (std::getline(in, line))
        {
            if (!line.empty())
            {
                last = line;
            }
        }

        if (last.empty())
        {
            std::cout << "empty\n";
        }
        else
        {
            std::cout << last << '\n';
        }
    }

    llt::stop_async_logger();
    return 0;
}