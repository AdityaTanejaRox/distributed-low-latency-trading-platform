#pragma once

#include "llt/types.hpp"

#include <optional>
#include <string>

namespace llt
{
    struct KrakenDemoCredentials
    {
        std::string api_key;
        std::string api_secret_base64;
    };

    struct KrakenDemoOrderRequest
    {
        std::string symbol{"PI_XBTUSD"};
        std::string side{"buy"};
        std::string order_type{"lmt"};
        std::string size{"1"};
        std::string limit_price{"1000"};
        std::string cli_ord_id{};
    };

    struct KrakenDemoOrderResult
    {
        bool transport_ok{false};
        int http_status{0};
        bool kraken_success{false};
        std::string raw_response{};
    };

    class KrakenFuturesDemoGateway
    {
    public:
        explicit KrakenFuturesDemoGateway(KrakenDemoCredentials creds);

        KrakenDemoOrderResult send_limit_order(const KrakenDemoOrderRequest& order);

        // Converts our internal NewOrder into a Kraken Futures demo limit order.
        KrakenDemoOrderResult send_internal_order_as_demo_limit(
            const NewOrder& order,
            const std::string& kraken_symbol
        );

    private:
        KrakenDemoCredentials creds_;
    };

    std::optional<KrakenDemoCredentials> load_kraken_demo_credentials_from_env();

    std::string kraken_futures_authent(
        const std::string& post_data,
        const std::string& nonce,
        const std::string& endpoint_path,
        const std::string& api_secret_base64
    );

    std::string current_time_millis_string();
}