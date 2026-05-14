#include "llt/logging.hpp"

int main() 
{
    llt::log(llt::LogLevel::Info, "oms_node", "standalone OMS node placeholder");
    llt::log(llt::LogLevel::Info, "oms_node", "in production this would enforce risk, dedupe orders, persist intent, and route to gateways");
    return 0;
}