#include "llt/logging.hpp"
#include "llt/multi_venue_router.hpp"
#include "llt/time.hpp"

#include <iostream>

using namespace llt;

static NormalizedMarketData make_md(
    MarketDataVenue venue,
    SymbolId symbol_id,
    const char* symbol,
    Price bid,
    Price ask,
    Sequence seq
)
{
    MarketDataUpdate u{};

    u.header.type = MsgType::MarketData;
    u.header.sequence = seq;
    u.header.recv_ts_ns = now_ns();

    u.symbol_id = symbol_id;
    u.symbol = Symbol{symbol};
    u.bid_px = bid;
    u.bid_qty = 100;
    u.ask_px = ask;
    u.ask_qty = 100;
    u.exchange_sequence = seq;

    return NormalizedMarketData{
        .venue = venue,
        .update = u
    };
}

int main()
{
    start_async_logger("logs/multi_venue_router_demo.log");

    MultiVenueRouter router;

    router.update(make_md(MarketDataVenue::Coinbase, 1, "BTC-USD", 8079000, 8079100, 1));
    router.update(make_md(MarketDataVenue::Binance, 1, "BTCUSDT", 8078950, 8079050, 1));
    router.update(make_md(MarketDataVenue::Hyperliquid, 1, "BTC", 8079010, 8079060, 1));

    Signal buy{};
    buy.symbol_id = 1;
    buy.side = Side::Buy;
    buy.qty = 1;

    auto decision = router.route_signal(buy);

    std::cout
        << "routeable=" << decision.routeable
        << " venue=" << to_string(decision.venue)
        << " side=" << to_string(decision.side)
        << " px=" << decision.limit_px
        << " reason=" << decision.reason
        << '\n';

    Signal sell{};
    sell.symbol_id = 1;
    sell.side = Side::Sell;
    sell.qty = 1;

    auto sell_decision = router.route_signal(sell);

    std::cout
        << "routeable=" << sell_decision.routeable
        << " venue=" << to_string(sell_decision.venue)
        << " side=" << to_string(sell_decision.side)
        << " px=" << sell_decision.limit_px
        << " reason=" << sell_decision.reason
        << '\n';

    stop_async_logger();
    return 0;
}