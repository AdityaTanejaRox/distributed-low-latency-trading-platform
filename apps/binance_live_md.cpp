#include "llt/live_market_data_connectors.hpp"
#include "llt/logging.hpp"
using namespace llt;
int main() 
{
    start_async_logger("logs/binance_live_md.log");

    log(llt::LogLevel::Info, "binance_live_md", "starting Binance live market data connector");

    const bool ok = llt::run_binance_live_book_ticker("btcusdt", 0);

    log(
        ok ? llt::LogLevel::Info : llt::LogLevel::Error,
        "binance_live_md",
        ok ? "completed Binance live market data connector" : "Binance live connector failed"
    );

    stop_async_logger();
    return ok ? 0 : 1;
}