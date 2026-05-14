#include "llt/logging.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

int main() 
{
    llt::start_async_logger("logs/itch_feed_generator.log");

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19191);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) 
    {
        llt::log(llt::LogLevel::Error, "itch_gen", "bind failed");
        return 1;
    }

    ::listen(fd, 1);

    llt::log(llt::LogLevel::Info, "itch_gen", "listening on 127.0.0.1:19191");

    const int client = ::accept(fd, nullptr, nullptr);

    if (client < 0) 
    {
        llt::log(llt::LogLevel::Error, "itch_gen", "accept failed");
        return 1;
    }

    for (int i = 0; i < 20; ++i) 
    {
        const long seq = 1000 + i;
        const long bid = 42125 + i;
        const long ask = bid + 1;

        const std::string line =
            "Q|" + std::to_string(seq) +
            "|MSFT|" + std::to_string(bid) +
            "|300|" + std::to_string(ask) +
            "|400\n";

        ::send(client, line.data(), line.size(), MSG_NOSIGNAL);

        llt::log(llt::LogLevel::Info, "itch_gen", "sent ITCH-style quote line");

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ::close(client);
    ::close(fd);

    llt::log(llt::LogLevel::Info, "itch_gen", "stopped");
    llt::stop_async_logger();
    return 0;
}