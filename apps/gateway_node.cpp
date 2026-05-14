#include "llt/logging.hpp"

int main() 
{
    llt::log(llt::LogLevel::Info, "gateway_node", "standalone gateway node placeholder");
    llt::log(llt::LogLevel::Info, "gateway_node", "in production this would maintain exchange sessions, sequence outbound orders, and process acks/fills");
    return 0;
}