#include "llt/logging.hpp"
#include "llt/market_data_adapter.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>

int main() {
    llt::start_async_logger("logs/itch_live_md.log");

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19191);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        llt::log(llt::LogLevel::Error, "itch_live", "connect failed");
        return 1;
    }

    std::string buffer;
    char temp[256];

    while (true) {
        const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);

        if (n <= 0) {
            break;
        }

        buffer.append(temp, temp + n);

        std::size_t pos = 0;

        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            auto md = llt::normalize_simulated_itch_quote(line, 4);

            if (md) {
                const auto& u = md->update;

                std::cout
                    << "venue=" << llt::to_string(md->venue)
                    << " symbol=" << u.symbol.str()
                    << " seq=" << u.exchange_sequence
                    << " bid=" << u.bid_px
                    << " ask=" << u.ask_px
                    << '\n';

                llt::log(llt::LogLevel::Info, "itch_live", "normalized live ITCH-style quote");
            }
        }
    }

    ::close(fd);

    llt::log(llt::LogLevel::Info, "itch_live", "stopped");
    llt::stop_async_logger();
    return 0;
}