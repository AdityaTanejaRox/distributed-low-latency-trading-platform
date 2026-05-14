#include "llt/live_market_data_connectors.hpp"
#include "llt/logging.hpp"
using namespace llt;
int main() 
{
    start_async_logger("logs/coinbase_live_md.log");

    log(llt::LogLevel::Info, "coinbase_live_md", "starting Coinbase live market data connector");

    const bool ok = llt::run_coinbase_live_ticker("BTC-USD", 0);

    log(
        ok ? llt::LogLevel::Info : llt::LogLevel::Error,
        "coinbase_live_md",
        ok ? "completed Coinbase live market data connector" : "Coinbase live connector failed"
    );

    stop_async_logger();
    return ok ? 0 : 1;
}