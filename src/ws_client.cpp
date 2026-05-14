#include "llt/ws_client.hpp"
#include "llt/logging.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <memory>

namespace llt 
{

    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace net = boost::asio;
    namespace ssl = boost::asio::ssl;
    using tcp = net::ip::tcp;

    struct WssClient::Impl 
    {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        tcp::resolver resolver{ioc};
        websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};
    };

    WssClient::WssClient(std::string host, std::string port, std::string target)
        : host_(std::move(host)),
        port_(std::move(port)),
        target_(std::move(target)),
        impl_(new Impl{}) {}

    bool WssClient::connect() 
    {
        try 
        {
            impl_->ctx.set_default_verify_paths();
            impl_->ctx.set_verify_mode(ssl::verify_none);

            auto results = impl_->resolver.resolve(host_, port_);

            auto& lowest = beast::get_lowest_layer(impl_->ws);
            net::connect(lowest, results.begin(), results.end());

            // SNI is required by many TLS endpoints.
            if (!SSL_set_tlsext_host_name(impl_->ws.next_layer().native_handle(), host_.c_str())) 
            {
                log(LogLevel::Error, "wss_client", "failed to set SNI hostname");
                return false;
            }

            impl_->ws.next_layer().handshake(ssl::stream_base::client);
            impl_->ws.handshake(host_ + ":" + port_, target_);

            log(LogLevel::Info, "wss_client", "connected websocket");
            return true;
        } 
        catch (const std::exception& e) 
        {
            log(LogLevel::Error, "wss_client", e.what());
            return false;
        }
    }

    bool WssClient::write_text(const std::string& msg) 
    {
        try 
        {
            impl_->ws.text(true);
            impl_->ws.write(net::buffer(msg));
            return true;
        } 
        catch (const std::exception& e) {
            log(LogLevel::Error, "wss_client", e.what());
            return false;
        }
    }

    bool WssClient::read_text(std::string& out) 
    {
        try 
        {
            beast::flat_buffer buffer;
            impl_->ws.read(buffer);
            out = beast::buffers_to_string(buffer.data());
            return true;
        } 
        catch (const std::exception& e) 
        {
            log(LogLevel::Error, "wss_client", e.what());
            return false;
        }
    }

    void WssClient::close() 
    {
        try 
        {
            impl_->ws.close(websocket::close_code::normal);
        } 
        catch (...) 
        {
        }
    }

}