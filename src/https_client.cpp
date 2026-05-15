#include "llt/https_client.hpp"
#include "llt/logging.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <exception>

namespace llt
{
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    namespace ssl = boost::asio::ssl;
    using tcp = net::ip::tcp;

    HttpsClient::HttpsClient(std::string host, std::string port)
        : host_(std::move(host)),
          port_(std::move(port))
    {
    }

    HttpResponse HttpsClient::post_form(
        const std::string& target,
        const std::string& body,
        const std::map<std::string, std::string>& headers
    )
    {
        try
        {
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};

            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_none);

            tcp::resolver resolver{ioc};
            beast::ssl_stream<tcp::socket> stream{ioc, ctx};

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str()))
            {
                log(LogLevel::Error, "https_client", "failed to set SNI hostname");
                return {};
            }

            auto const results = resolver.resolve(host_, port_);
            net::connect(stream.next_layer(), results.begin(), results.end());

            stream.handshake(ssl::stream_base::client);

            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, host_);
            req.set(http::field::user_agent, "llt-kraken-futures-demo-gateway");
            req.set(http::field::content_type, "application/x-www-form-urlencoded");

            for (const auto& [k, v] : headers)
            {
                req.set(k, v);
            }

            req.body() = body;
            req.prepare_payload();

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;

            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            return HttpResponse{
                static_cast<int>(res.result_int()),
                res.body()
            };
        }
        catch (const std::exception& e)
        {
            log(LogLevel::Error, "https_client", e.what());
            return {};
        }
    }

    HttpResponse HttpsClient::get(
        const std::string& target,
        const std::map<std::string, std::string>& headers
    )
    {
        try
        {
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};

            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_none);

            tcp::resolver resolver{ioc};
            beast::ssl_stream<tcp::socket> stream{ioc, ctx};

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str()))
            {
                log(LogLevel::Error, "https_client", "failed to set SNI hostname");
                return {};
            }

            auto const results = resolver.resolve(host_, port_);
            net::connect(stream.next_layer(), results.begin(), results.end());

            stream.handshake(ssl::stream_base::client);

            http::request<http::empty_body> req{http::verb::get, target, 11};
            req.set(http::field::host, host_);
            req.set(http::field::user_agent, "llt-kraken-futures-demo-gateway");

            for (const auto& [k, v] : headers)
            {
                req.set(k, v);
            }

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;

            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            return HttpResponse{
                static_cast<int>(res.result_int()),
                res.body()
            };
        }
        catch (const std::exception& e)
        {
            log(LogLevel::Error, "https_client", e.what());
            return {};
        }
    }
}