#include "llt/logging.hpp"
#include "llt/replay.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace llt;

int main(int argc, char** argv)
{
    start_async_logger("logs/replay_merge.log");

    if (argc < 3)
    {
        std::cerr
            << "usage:\n"
            << "  ./build/replay_merge <output.bin> <input1.bin> <input2.bin> ...\n";

        stop_async_logger();
        return 1;
    }

    const std::string output_path = argv[1];

    std::vector<ReplayEvent> events;

    for (int i = 2; i < argc; ++i)
    {
        ReplayReader reader{argv[i]};

        if (!reader.open())
        {
            std::cerr << "failed to open " << argv[i] << '\n';
            continue;
        }

        while (auto event = reader.next())
        {
            events.push_back(*event);
        }

        reader.close();
    }

    std::sort(
        events.begin(),
        events.end(),
        [](const ReplayEvent& a, const ReplayEvent& b)
        {
            if (a.header.timestamp_ns != b.header.timestamp_ns)
            {
                return a.header.timestamp_ns < b.header.timestamp_ns;
            }

            if (a.header.sequence != b.header.sequence)
            {
                return a.header.sequence < b.header.sequence;
            }

            return static_cast<int>(a.header.node_role) <
                   static_cast<int>(b.header.node_role);
        }
    );

    ReplayWriter writer{
        output_path,
        ReplayWriteMode::Truncate,
        NodeRole::Simulator
    };

    if (!writer.open())
    {
        std::cerr << "failed to open merged output\n";
        stop_async_logger();
        return 1;
    }

    for (const auto& event : events)
    {
        writer.append_event(event);
    }

    writer.close();

    std::cout << "merged_events=" << events.size() << '\n';
    std::cout << "output=" << output_path << '\n';

    stop_async_logger();
    return 0;
}