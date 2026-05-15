#include "llt/replay.hpp"
#include "llt/logging.hpp"
#include "llt/time.hpp"

#include <iostream>

using namespace llt;

int main()
{
    start_async_logger(
        "logs/replay_demo.log"
    );

    ReplayWriter writer{
        "journals/replay.bin"
    };

    writer.open();

    Envelope e{};

    e.type=
    MsgType::MarketData;

    e.payload.market_data.symbol=
        Symbol{"BTC"};

    e.payload.market_data.bid_px=
        8150000;

    e.payload.market_data.ask_px=
        8150100;

    writer.append(
        e,
        1,
        now_ns()
    );

    writer.close();

    ReplayReader reader{
        "journals/replay.bin"
    };

    reader.open();

    while(
        auto event=
        reader.next()
    )
    {
        std::cout
            <<"seq="
            <<event->seq
            <<" symbol="
            <<event->envelope
                  .payload
                  .market_data
                  .symbol
                  .str()
            <<" bid="
            <<event->envelope
                  .payload
                  .market_data
                  .bid_px
            <<" ask="
            <<event->envelope
                  .payload
                  .market_data
                  .ask_px
            <<'\n';
    }

    reader.close();

    stop_async_logger();
}