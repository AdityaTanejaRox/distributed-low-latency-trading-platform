#include "llt/logging.hpp"

int main() 
{
    llt::log(llt::LogLevel::Info, "strategy_node", "standalone strategy node placeholder");
    llt::log(llt::LogLevel::Info, "strategy_node", "in production this would consume sequenced market data and emit deterministic signals");
    return 0;
}