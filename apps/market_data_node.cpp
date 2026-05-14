#include "llt/logging.hpp"

int main() 
{
    llt::log(llt::LogLevel::Info, "market_data_node", "standalone market data node placeholder");
    llt::log(llt::LogLevel::Info, "market_data_node", "in production this would decode UDP/TCP multicast feeds and publish normalized market data");
    return 0;
}