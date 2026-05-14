#pragma once

#include "llt/spsc_queue.hpp"
#include "llt/types.hpp"

namespace llt 
{

    constexpr std::size_t BUS_CAPACITY = 1 << 14;

    struct LocalBus 
    {
        SpscQueue<Envelope, BUS_CAPACITY> market_to_strategy;
        SpscQueue<Envelope, BUS_CAPACITY> strategy_to_oms;
        SpscQueue<Envelope, BUS_CAPACITY> oms_to_gateway;
        SpscQueue<Envelope, BUS_CAPACITY> gateway_to_oms;
    };

}