#include "llt/kraken_futures_demo_gateway.hpp"

#include "llt/https_client.hpp"
#include "llt/logging.hpp"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>

namespace llt
{
    namespace
    {
        constexpr const char* KRAKEN_DEMO_HOST = "demo-futures.kraken.com";
        constexpr const char* KRAKEN_DEMO_PORT = "443";

        // Kraken Futures docs show authenticated v3 calls using:
        //
        //   /derivatives/api/v3/<endpoint>
        //
        // Send order endpoint:
        //
        //   /derivatives/api/v3/sendorder
        //
        // The endpointPath used for Authent must match the URL extension.
        constexpr const char* SEND_ORDER_ENDPOINT = "/derivatives/api/v3/sendorder";

        std::string url_encode(const std::string& s)
        {
            std::ostringstream out;

            for (unsigned char c : s)
            {
                if ((c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == '~')
                {
                    out << c;
                }
                else
                {
                    out << '%' << std::uppercase << std::hex
                        << std::setw(2) << std::setfill('0')
                        << static_cast<int>(c)
                        << std::nouppercase << std::dec;
                }
            }

            return out.str();
        }

        std::vector<unsigned char> base64_decode(const std::string& input)
        {
            BIO* bio = BIO_new_mem_buf(input.data(), static_cast<int>(input.size()));
            BIO* b64 = BIO_new(BIO_f_base64());

            BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
            bio = BIO_push(b64, bio);

            std::vector<unsigned char> out(input.size());
            const int len = BIO_read(bio, out.data(), static_cast<int>(out.size()));

            BIO_free_all(bio);

            if (len <= 0)
            {
                return {};
            }

            out.resize(static_cast<std::size_t>(len));
            return out;
        }

        std::string base64_encode(const unsigned char* data, std::size_t len)
        {
            BIO* b64 = BIO_new(BIO_f_base64());
            BIO* mem = BIO_new(BIO_s_mem());

            BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
            b64 = BIO_push(b64, mem);

            BIO_write(b64, data, static_cast<int>(len));
            BIO_flush(b64);

            BUF_MEM* buffer_ptr = nullptr;
            BIO_get_mem_ptr(b64, &buffer_ptr);

            std::string out(buffer_ptr->data, buffer_ptr->length);

            BIO_free_all(b64);
            return out;
        }

        std::string side_to_kraken(Side side)
        {
            return side == Side::Buy ? "buy" : "sell";
        }

        std::string ticks_to_price_string(Price ticks)
        {
            // Internal price scale is cents/ticks = 100.
            //
            // Example:
            //   8153183 -> "81531.83"
            std::ostringstream oss;
            oss << (ticks / 100) << "." << std::setw(2) << std::setfill('0')
                << std::llabs(ticks % 100);
            return oss.str();
        }
    }

    std::string current_time_millis_string()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return std::to_string(ms);
    }

    std::optional<KrakenDemoCredentials> load_kraken_demo_credentials_from_env()
    {
        const char* key = std::getenv("KRAKEN_FUTURES_DEMO_API_KEY");
        const char* secret = std::getenv("KRAKEN_FUTURES_DEMO_API_SECRET");

        if (key == nullptr || secret == nullptr)
        {
            log(LogLevel::Error, "kraken_demo", "missing KRAKEN_FUTURES_DEMO_API_KEY or KRAKEN_FUTURES_DEMO_API_SECRET");
            return std::nullopt;
        }

        return KrakenDemoCredentials{
            .api_key = key,
            .api_secret_base64 = secret
        };
    }

    std::string kraken_futures_authent(
        const std::string& post_data,
        const std::string& nonce,
        const std::string& endpoint_path,
        const std::string& api_secret_base64
    )
    {
        // Kraken Futures Authent:
        //
        //   1. concatenate postData + Nonce + endpointPath
        //   2. SHA256 the concatenated bytes
        //   3. base64-decode api_secret
        //   4. HMAC-SHA512 over SHA256 digest
        //   5. base64-encode the HMAC output
        //
        // See Kraken Futures REST auth docs.

        const std::string message = post_data + nonce + endpoint_path;

        unsigned char sha256_digest[SHA256_DIGEST_LENGTH];

        SHA256(
            reinterpret_cast<const unsigned char*>(message.data()),
            message.size(),
            sha256_digest
        );

        const auto decoded_secret = base64_decode(api_secret_base64);

        if (decoded_secret.empty())
        {
            log(LogLevel::Error, "kraken_demo", "failed to base64-decode API secret");
            return {};
        }

        unsigned int hmac_len = 0;
        unsigned char hmac_result[EVP_MAX_MD_SIZE];

        HMAC(
            EVP_sha512(),
            decoded_secret.data(),
            static_cast<int>(decoded_secret.size()),
            sha256_digest,
            SHA256_DIGEST_LENGTH,
            hmac_result,
            &hmac_len
        );

        return base64_encode(hmac_result, hmac_len);
    }

    KrakenFuturesDemoGateway::KrakenFuturesDemoGateway(KrakenDemoCredentials creds)
        : creds_(std::move(creds))
    {
    }

    KrakenDemoOrderResult KrakenFuturesDemoGateway::send_limit_order(
        const KrakenDemoOrderRequest& order
    )
    {
        const std::string nonce = current_time_millis_string();

        // Keep postData order stable because Authent must sign the exact body.
        //
        // Parameters are based on Kraken Futures sendorder:
        //
        //   orderType = lmt / post / ioc / mkt / stp / take_profit / trailing_stop
        //   symbol    = futures contract symbol, e.g. PI_XBTUSD
        //   side      = buy / sell
        //   size      = order size
        //   limitPrice= required for limit order
        //   cliOrdId  = optional client order id
        //
        // The body is form-url-encoded and the exact same string is used
        // for Authent generation.
        std::string post_data =
            "orderType=" + url_encode(order.order_type) +
            "&symbol=" + url_encode(order.symbol) +
            "&side=" + url_encode(order.side) +
            "&size=" + url_encode(order.size) +
            "&limitPrice=" + url_encode(order.limit_price);

        if (!order.cli_ord_id.empty())
        {
            post_data += "&cliOrdId=" + url_encode(order.cli_ord_id);
        }

        const std::string authent =
            kraken_futures_authent(
                post_data,
                nonce,
                SEND_ORDER_ENDPOINT,
                creds_.api_secret_base64
            );

        if (authent.empty())
        {
            return KrakenDemoOrderResult{
                .transport_ok = false,
                .http_status = 0,
                .kraken_success = false,
                .raw_response = "authent generation failed"
            };
        }

        HttpsClient client{KRAKEN_DEMO_HOST, KRAKEN_DEMO_PORT};

        const auto response = client.post_form(
            SEND_ORDER_ENDPOINT,
            post_data,
            {
                {"APIKey", creds_.api_key},
                {"Nonce", nonce},
                {"Authent", authent}
            }
        );

        const bool http_ok = response.status_code >= 200 && response.status_code < 300;

        // Kraken returns JSON. We keep parsing minimal here because the goal is
        // to preserve raw exchange response for debugging and OMS mapping.
        const bool kraken_success =
            response.body.find(R"("result":"success")") != std::string::npos ||
            response.body.find(R"("result": "success")") != std::string::npos;

        return KrakenDemoOrderResult{
            .transport_ok = http_ok,
            .http_status = response.status_code,
            .kraken_success = kraken_success,
            .raw_response = response.body
        };
    }

    KrakenDemoOrderResult KrakenFuturesDemoGateway::send_internal_order_as_demo_limit(
        const NewOrder& order,
        const std::string& kraken_symbol
    )
    {
        KrakenDemoOrderRequest req{};

        req.symbol = kraken_symbol;
        req.side = side_to_kraken(order.side);
        req.order_type = "lmt";

        // Demo default:
        //
        // Kraken futures size is contract size.
        // Our internal Quantity may be micro-units from crypto market data,
        // so for the order gateway demo we force a safe small size.
        req.size = "1";

        req.limit_price = ticks_to_price_string(order.limit_px);

        req.cli_ord_id =
            "llt-" + std::to_string(order.client_order_id) +
            "-" + current_time_millis_string();

        return send_limit_order(req);
    }
}