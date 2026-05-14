#include "llt/live_market_data_connectors.hpp"
#include "llt/logging.hpp"
using namespace llt;
int main() 
{
    start_async_logger("logs/hyperliquid_live_md.log");

    log(llt::LogLevel::Info, "hyperliquid_live_md", "starting Hyperliquid live market data connector");

    const bool ok = llt::run_hyperliquid_live_l2book("BTC", 0);

    log(
        ok ? llt::LogLevel::Info : llt::LogLevel::Error,
        "hyperliquid_live_md",
        ok ? "completed Hyperliquid live market data connector" : "Hyperliquid live connector failed"
    );

    stop_async_logger();
    return ok ? 0 : 1;
}