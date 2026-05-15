#include "llt/kraken_futures_demo_gateway.hpp"
#include "llt/logging.hpp"
#include "llt/time.hpp"
#include "llt/types.hpp"

#include <iostream>

using namespace llt;

int main()
{
    start_async_logger("logs/kraken_futures_demo_order.log");

    log(LogLevel::Info, "kraken_demo_app", "starting Kraken Futures demo order test");

    auto creds = load_kraken_demo_credentials_from_env();

    if (!creds)
    {
        std::cerr
            << "Missing credentials.\n"
            << "Set:\n"
            << "  export KRAKEN_FUTURES_DEMO_API_KEY=\"...\"\n"
            << "  export KRAKEN_FUTURES_DEMO_API_SECRET=\"...\"\n";

        stop_async_logger();
        return 1;
    }

    KrakenFuturesDemoGateway gateway{*creds};

    // This is an intentionally far-away passive BUY limit order.
    //
    // Why?
    //
    // We want to prove authenticated testnet order submission without trying
    // to aggressively cross the spread.
    //
    // Kraken Futures demo symbol:
    //
    //   PI_XBTUSD
    //
    // If this symbol is not available in your demo account, check the
    // available instruments in the web UI and change symbol accordingly.
    KrakenDemoOrderRequest req{};
    req.symbol = "PI_XBTUSD";
    req.side = "buy";
    req.order_type = "lmt";
    req.size = "1";
    req.limit_price = "1000";
    req.cli_ord_id = "llt-demo-" + current_time_millis_string();

    const auto result = gateway.send_limit_order(req);

    std::cout << "http_status=" << result.http_status << '\n';
    std::cout << "transport_ok=" << result.transport_ok << '\n';
    std::cout << "kraken_success=" << result.kraken_success << '\n';
    std::cout << "raw_response=" << result.raw_response << '\n';

    if (result.kraken_success)
    {
        log(LogLevel::Info, "kraken_demo_app", "Kraken Futures demo order submitted successfully");
    }
    else
    {
        log(LogLevel::Error, "kraken_demo_app", "Kraken Futures demo order submission failed");
    }

    stop_async_logger();
    return result.kraken_success ? 0 : 1;
}