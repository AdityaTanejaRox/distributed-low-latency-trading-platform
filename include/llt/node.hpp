#pragma once

#include "llt/types.hpp"

#include <atomic>
#include <string>

namespace llt 
{
    class NodeHealth 
    {
        public:
            NodeHealth(NodeId id, NodeRole role);

            Heartbeat heartbeat();

            void observe_heartbeat(const Heartbeat& hb);

            bool is_peer_stale(TimestampNs stale_after_ns) const;

        private:
            NodeId id_;
            NodeRole role_;
            TimestampNs last_peer_seen_ns_{0};
            Sequence sequence_{0};
    };
}