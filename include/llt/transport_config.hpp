#pragma once

#include <cstddef>
#include <cstdint>

namespace llt 
{

    // ============================================================
    // Transport Configuration
    // ============================================================

    // Heartbeat interval.
    //
    // Every connected peer periodically emits a heartbeat frame.
    // This allows stale peer detection.
    constexpr std::uint64_t HEARTBEAT_INTERVAL_MS = 1000;

    // If we do not receive heartbeat traffic for this duration,
    // we consider the peer stale/dead/disconnected.
    constexpr std::uint64_t HEARTBEAT_TIMEOUT_MS = 3000;

    // Reconnect retry interval.
    constexpr std::uint64_t RECONNECT_INTERVAL_MS = 1000;

    // Socket send buffer.
    constexpr int SOCKET_SNDBUF_SIZE = 1 << 20;

    // Socket receive buffer.
    constexpr int SOCKET_RCVBUF_SIZE = 1 << 20;

    // Backpressure thresholds.
    //
    // These are placeholders for future queue pressure tracking.
    constexpr std::size_t QUEUE_HIGH_WATERMARK = 8192;
    constexpr std::size_t QUEUE_CRITICAL_WATERMARK = 12000;

    // Policy:
    //
    // NORMAL
    //   queue < high watermark
    //
    // THROTTLE
    //   queue >= high watermark
    //
    // FAIL_CLOSED
    //   queue >= critical watermark

}