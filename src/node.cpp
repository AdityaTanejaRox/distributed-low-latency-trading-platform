#include "llt/node.hpp"
#include "llt/time.hpp"

namespace llt 
{
    NodeHealth::NodeHealth(NodeId id, NodeRole role)
        : id_(id),
        role_(role) {}

    Heartbeat NodeHealth::heartbeat() 
    {
        Heartbeat hb{};
        hb.header.type = MsgType::Heartbeat;
        hb.header.source_node = id_;
        hb.header.sequence = ++sequence_;
        hb.header.send_ts_ns = now_ns();
        hb.role = role_;
        hb.last_seen_ns = now_ns();
        return hb;
    }

    void NodeHealth::observe_heartbeat(const Heartbeat& hb) 
    {
        last_peer_seen_ns_ = hb.header.recv_ts_ns;
    }

    bool NodeHealth::is_peer_stale(TimestampNs stale_after_ns) const 
    {
        if (last_peer_seen_ns_ == 0) 
        {
            return true;
        }

        return now_ns() - last_peer_seen_ns_ > stale_after_ns;
    }
}