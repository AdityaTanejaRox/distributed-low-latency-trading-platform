#include "llt/logging.hpp"
#include "llt/multi_symbol_order_book.hpp"
#include "llt/time.hpp"

#include <iostream>

using namespace llt;

static MarketDataUpdate make_md(
    SymbolId id,
    const char* symbol,
    Price bid,
    Price ask,
    Sequence seq
)
{
    MarketDataUpdate md{};

    md.header.type = MsgType::MarketData;
    md.header.sequence = seq;
    md.header.recv_ts_ns = now_ns();

    md.symbol_id = id;
    md.symbol = Symbol{symbol};
    md.bid_px = bid;
    md.bid_qty = 100;
    md.ask_px = ask;
    md.ask_qty = 200;
    md.exchange_sequence = seq;

    return md;
}

int main()
{
    start_async_logger("logs/multi_symbol_book_demo.log");

    MultiSymbolTopOfBook books;

    books.apply(make_md(1, "BTC-USD", 8079000, 8079100, 1));
    books.apply(make_md(2, "ETH-USD", 350000, 350010, 1));
    books.apply(make_md(3, "SOL-USD", 18000, 18001, 1));

    for (const auto& book : books.snapshots())
    {
        std::cout
            << "symbol=" << book.symbol.str()
            << " bid=" << book.bid_px
            << " ask=" << book.ask_px
            << " seq=" << book.exchange_sequence
            << '\n';
    }

    std::cout << "symbol_count=" << books.symbol_count() << '\n';

    stop_async_logger();
    return 0;
}