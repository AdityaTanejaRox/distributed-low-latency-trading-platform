# Distributed Low-Latency Trading Platform

## Overview

This project is a C++20 distributed low-latency trading platform skeleton.

It models the core architecture used in professional trading systems:

1. Market data ingestion
2. Strategy execution
3. Order management
4. Exchange connectivity
5. Deterministic sequencing
6. Bounded queues
7. Fault isolation
8. Tail-latency control
9. Risk checks
10. Heartbeat-based node health

The goal is not to build a fake toy trading app.

The goal is to show how a real low-latency trading platform should be decomposed, why each component exists, how messages flow through the system, and how the architecture deals with bursty market data, network partitions, node failures, exchange disconnects, and deterministic behavior requirements.

This is designed as an interview-grade systems project for low-latency C++, HFT infrastructure, exchange connectivity, market data systems, and trading platform engineering roles.

---

## Problem Statement

Design a distributed low-latency trading platform where market data ingestion, strategy execution, order management, and exchange connectivity are deployed across multiple nodes, while guaranteeing deterministic behavior, ultra-low tail latency, and fault isolation under network partitions, hardware failures, and bursty market conditions.

The platform should support:

- Market data ingestion from external venues
- Normalization into internal market data format
- Deterministic strategy processing
- Risk validation before orders leave the system
- Order lifecycle management
- Exchange gateway isolation
- Heartbeats and health monitoring
- Bounded queues to avoid unbounded memory growth
- Sequence numbers for replayability and deterministic recovery
- Local simulation mode for testing the full path

---

## Why This Problem Is Hard

A trading platform is not just a collection of services.

It is a latency-sensitive distributed system where correctness and timing both matter.

The difficult parts are:

1. Market data arrives in bursts.
2. Strategies must process data deterministically.
3. Orders must not be duplicated.
4. Risk checks must happen before external side effects.
5. Exchange gateways can disconnect.
6. Network partitions can isolate nodes.
7. Hardware failures can kill one node but should not kill the whole platform.
8. Queues must not grow without bound.
9. Tail latency matters more than average latency.
10. Recovery must not create inconsistent order state.

A normal backend system may optimize for throughput and availability.

A trading system must optimize for deterministic state transitions, predictable latency, fault containment, and explicit failure behavior.

---

## Architecture

The platform is split into four major runtime roles:

```text
+-------------------+        +-------------------+        +-------------------+        +-------------------+
| Market Data Node  | -----> |  Strategy Node    | -----> |     OMS Node      | -----> | Exchange Gateway  |
+-------------------+        +-------------------+        +-------------------+        +-------------------+
        |                            |                            |                            |
        | normalized MD              | trading signals             | risk-checked orders          | exchange protocol
        v                            v                            v                            v
   MarketDataUpdate                Signal                     NewOrder                      Ack/Fill/Reject
```

---

## Component Responsibilities

### 1. Market Data Node

The market data node is responsible for receiving external market data and converting it into the internal format.

In a real production system this could consume:

- UDP multicast feeds
- TCP recovery feeds
- WebSocket feeds
- FIX market data
- Binary proprietary exchange protocols

In this project, the simulator creates synthetic market data updates.

The important design choice is that the rest of the system does not care about the external exchange format.

Everything downstream receives a normalized `MarketDataUpdate`.

---

### 2. Strategy Node

The strategy node consumes market data and emits trading signals.

It does not directly send orders to the exchange.

This is intentional.

A strategy should express intent, not perform irreversible side effects.

The strategy emits a `Signal` containing:

- Symbol
- Side
- Limit price
- Quantity
- Confidence score

The OMS decides whether the signal is allowed to become an order.

This separation makes the system safer and easier to test.

---

### 3. Order Management System

The OMS is the control point of the platform.

It is responsible for:

- Converting signals into orders
- Assigning client order IDs
- Enforcing risk limits
- Preventing duplicate orders
- Tracking live order state
- Processing gateway acknowledgements
- Processing gateway rejects
- Maintaining deterministic order lifecycle state

The OMS is where trading intent becomes an actual order.

That means the OMS must be deterministic and conservative.

If the OMS is unsure whether it is safe to trade, it should reject or stop routing.

---

### 4. Exchange Gateway

The exchange gateway isolates venue-specific connectivity.

It is responsible for:

- Sending orders to the exchange
- Maintaining exchange sessions
- Handling disconnects
- Receiving acknowledgements
- Receiving fills
- Receiving rejects
- Translating exchange protocol messages back into internal messages

In this project, the gateway is simulated.

In production, each exchange gateway would likely be a separate process or host.

This prevents one bad exchange session from poisoning the rest of the trading stack.

---

## Message Flow

The local simulation uses this flow:

```text
MarketDataUpdate
    -> StrategyEngine
        -> Signal
            -> OrderManager
                -> NewOrder
                    -> ExchangeGateway
                        -> Ack / Reject
                            -> OrderManager
```

The project models this using bounded SPSC queues.

The queues are:

- `market_to_strategy`
- `strategy_to_oms`
- `oms_to_gateway`
- `gateway_to_oms`

Each queue has fixed capacity.

That is intentional.

---

## Why Bounded Queues?

In low-latency systems, unbounded queues are dangerous.

An unbounded queue hides overload.

If market data arrives faster than the strategy can process it, an unbounded queue will keep growing.

That causes:

- Memory pressure
- Cache misses
- Latency spikes
- Eventually, process death

A bounded queue makes overload explicit.

When the queue is full, the system must choose a policy:

- Drop stale market data
- Backpressure the producer
- Disable trading
- Fail closed
- Switch to recovery mode

For trading, silently building up seconds of stale market data is unacceptable.

It is better to drop, reject, or stop trading than to trade on old state.

---

## Deterministic Behavior

Deterministic behavior means that given the same ordered input stream, the system produces the same output stream.

This project supports determinism through:

- Explicit sequence numbers
- Single-writer ownership per stateful component
- Bounded FIFO queues
- No shared mutable state across components
- Risk checks before side effects
- Clear message types
- Local state machines

The OMS owns order state.

The strategy owns strategy state.

The gateway owns exchange session state.

No component randomly mutates another component's state.

---

## Tail Latency Design

Ultra-low tail latency requires avoiding operations that unpredictably pause the hot path.

This project uses:

- Fixed-size messages
- Stack-allocated message structs
- Bounded SPSC queues
- No heap allocation in queue operations
- Simple branch-light message flow
- Monotonic timestamps
- Explicit overload visibility

Production improvements would include:

- CPU pinning
- NUMA-aware placement
- Huge pages
- Lock-free telemetry buffers
- Kernel bypass networking
- Busy polling
- Preallocated object pools
- Binary protocols
- `SO_REUSEPORT`
- `io_uring`
- DPDK or Solarflare/OpenOnload-style kernel bypass

---

## Fault Isolation

The architecture separates major concerns into different nodes.

This matters because failures are different by component.

### Market Data Failure

If market data becomes stale, the strategy should stop generating signals.

Trading on stale market data is dangerous.

### Strategy Failure

If the strategy crashes, the OMS and gateway can remain alive.

Existing live orders can still be cancelled or monitored.

### OMS Failure

The OMS is critical.

In production, OMS recovery would require replaying durable intent logs and exchange state.

### Gateway Failure

If a gateway disconnects, the OMS should stop routing orders to that venue.

Other gateways can continue operating.

### Network Partition

If the OMS cannot reach a gateway, it must fail closed.

Fail closed means no new orders should be sent through an uncertain path.

---

## Network Partitions

A network partition is worse than a clean crash.

With a crash, the system knows the peer is dead.

With a partition, both sides may still be alive but unable to communicate.

The platform handles this conceptually using:

- Heartbeats
- Last-seen timestamps
- Stale peer detection
- Gateway availability flags
- Conservative rejection policy

If the OMS has not heard from a gateway within the heartbeat timeout, it should mark the gateway unavailable.

When unavailable, new orders are rejected instead of sent.

This prevents ghost orders and inconsistent state.

---

## Bursty Market Conditions

Market data often arrives in bursts.

For example:

- Market open
- Economic releases
- Fed announcements
- Exchange reopen after halt
- Liquidation cascades
- Crypto volatility spikes

During bursts, the system must not allow latency to grow without bound.

This project demonstrates the basic mechanism:

- Bounded queues
- Drop counters
- Deterministic processing
- Explicit stale checks

A production market data system may also use conflation.

For top-of-book strategies, the latest quote may be more important than every intermediate quote.

For order book reconstruction, however, every sequence may matter.

That is why feed handlers often separate:

- Lossless book-building path
- Conflated strategy-consumption path

---

## Risk Management

Risk checks happen in the OMS before an order reaches the gateway.

The sample risk engine validates:

- Maximum position
- Maximum order quantity
- Maximum notional

The OMS rejects unsafe orders before they leave the process.

This is important because once an order reaches the exchange, it becomes an external side effect.

Production risk would also include:

- Max order rate
- Max open orders
- Fat-finger price bands
- Self-trade prevention
- Kill switch
- Per-symbol exposure
- Per-strategy exposure
- Per-venue exposure
- Credit limits
- Drop-copy reconciliation

---

## Project Layout

```text
distributed-low-latency-trading-platform/
├── CMakeLists.txt
├── README.md
├── apps/
│   ├── gateway_node.cpp
│   ├── market_data_node.cpp
│   ├── oms_node.cpp
│   ├── simulator.cpp
│   └── strategy_node.cpp
├── configs/
│   └── sample_config.yaml
├── include/
│   └── llt/
│       ├── exchange_gateway.hpp
│       ├── logging.hpp
│       ├── message_bus.hpp
│       ├── node.hpp
│       ├── oms.hpp
│       ├── order_book.hpp
│       ├── risk.hpp
│       ├── spsc_queue.hpp
│       ├── strategy.hpp
│       ├── time.hpp
│       └── types.hpp
└── src/
    ├── exchange_gateway.cpp
    ├── logging.cpp
    ├── node.cpp
    ├── oms.cpp
    ├── order_book.cpp
    ├── risk.cpp
    ├── strategy.cpp
    └── time.cpp
```

---

## Build Instructions

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

---

## Run the Full Local Simulation

```bash
./build/simulator
```

Expected output:

```json
{"level":"INFO","component":"simulator","message":"starting full local pipeline simulation"}
{"level":"INFO","component":"strategy","message":"generated signal"}
{"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"level":"INFO","component":"simulator","message":"simulation complete"}
```

---

## Run Individual Node Placeholders

```bash
./build/market_data_node
./build/strategy_node
./build/oms_node
./build/gateway_node
```

These binaries currently act as placeholders for a future multi-process deployment.

The full working path is implemented in `simulator.cpp`.

---

## Important Design Choices

### Why C++20?

C++ gives direct control over:

- Memory layout
- Allocation
- CPU cache behavior
- Object lifetimes
- Atomics
- Threading
- Networking
- Serialization
- Binary protocols

For low-latency trading infrastructure, this control is valuable.

---

### Why SPSC Queues?

SPSC means single-producer single-consumer.

Many trading pipelines naturally follow this model:

```text
feed handler thread -> strategy thread
strategy thread -> OMS thread
OMS thread -> gateway thread
```

SPSC queues are simpler and faster than general MPMC queues.

They avoid unnecessary contention when the topology is known.

---

### Why Not Shared State?

Shared state creates nondeterminism.

If multiple threads mutate the same order book or order state, behavior depends on timing.

That makes replay and debugging hard.

This project uses message passing instead.

Each component owns its state.

---

### Why Risk Before Gateway?

The gateway performs external side effects.

Once an order is sent to the exchange, it cannot be treated as purely internal state anymore.

Therefore, risk must happen before the gateway.

---

### Why Heartbeats?

Heartbeats detect peer liveness.

They are not perfect.

They cannot prove correctness.

But they allow the system to detect stale peers and fail closed.

In trading systems, failing closed is usually safer than continuing to trade through uncertain connectivity.

---

## What This Project Demonstrates

This project demonstrates:

- Low-latency message-passing architecture
- Fixed-size internal message types
- Bounded SPSC queues
- Market data normalization concept
- Strategy signal generation
- OMS order creation
- Risk validation
- Gateway ack/reject handling
- Heartbeat and stale peer model
- Clean separation of trading system responsibilities
- CMake-based C++20 project structure

---

## What This Project Does Not Yet Implement

This project does not yet include:

- Real sockets
- Real exchange protocol
- FIX
- UDP multicast
- Persistent journal
- Snapshot recovery
- Drop copy
- Multi-symbol order books
- Multi-venue routing
- CPU pinning
- NUMA placement
- Kernel bypass
- Real historical replay
- Prometheus metrics
- Full kill switch
- Production-grade logging

Those are natural extensions.

---

## Suggested Extensions

---

## Phase 1: Multi-Threaded Local Runtime

The original implementation executed the entire pipeline in a single thread:

```text
Market Data -> Strategy -> OMS -> Gateway
```

While functionally correct, this does not resemble how low-latency systems operate in production.

Real trading systems isolate independent stages onto dedicated threads and often dedicated CPU cores.

Reasons include:

- Reducing scheduler-induced latency
- Preventing CPU migration
- Improving cache locality
- Isolating failures
- Making latency deterministic
- Minimizing tail latency

This phase converts the local runtime from a single-threaded simulation into a CPU-pinned multi-threaded runtime.

The architecture now becomes:

```text
CPU 0
└── Main Runtime Thread

CPU 0
└── Market Data Thread
        |
        v

CPU 1
└── Strategy Thread
        |
        v

CPU 2
└── OMS Thread
        |
        v

CPU 3
└── Gateway Thread
```

Message flow remains:

```text
MarketData
    →
Strategy
    →
Signal
    →
OMS
    →
Order
    →
Gateway
    →
Ack/Reject
    →
OMS
```

The difference is that each stage now executes independently.

Communication between threads uses bounded SPSC queues.

---

### Why Thread Pinning Matters

Linux normally allows the scheduler to migrate threads across CPUs.

Migration can cause:

- L1 cache misses
- L2 cache misses
- TLB invalidation
- scheduler jitter
- NUMA effects
- tail latency spikes

For example:

```text
strategy thread
CPU2 → CPU6 → CPU1 → CPU4
```

Each migration potentially destroys cache warmth.

In low-latency systems this matters.

Even small scheduling events can produce:

```text
p50 = 2μs
p99 = 100μs
```

The average latency appears fine.

Tail latency becomes terrible.

Instead we pin threads:

```cpp
pin_current_thread_to_cpu(...)
```

using:

```cpp
pthread_setaffinity_np(...)
```

which guarantees:

```text
market data → CPU0
strategy → CPU1
OMS → CPU2
gateway → CPU3
```

Now:

- thread migration disappears
- cache locality improves
- latency becomes more deterministic

---

### Thread Naming

Thread names are also assigned:

```cpp
pthread_setname_np(...)
```

Examples:

```text
llt-md
llt-strategy
llt-oms
llt-gateway
```

These become visible in:

```bash
htop

ps -L

perf top

/proc/<pid>/task
```

This makes runtime debugging significantly easier.

---

### Thread Responsibilities

#### Market Data Thread

Responsibilities:

- generate synthetic market data
- assign exchange sequence numbers
- publish normalized messages
- push into strategy queue

CPU:

```text
CPU0
```

Queue:

```cpp
market_to_strategy
```

---

#### Strategy Thread

Responsibilities:

- consume market data
- maintain top-of-book state
- generate signals
- publish signals

CPU:

```text
CPU1
```

Queue:

```cpp
strategy_to_oms
```

---

#### OMS Thread

Responsibilities:

- receive signals
- run risk checks
- assign order IDs
- track live orders
- process acknowledgements
- process rejects

CPU:

```text
CPU2
```

Queues:

```cpp
strategy_to_oms
gateway_to_oms
oms_to_gateway
```

---

#### Gateway Thread

Responsibilities:

- simulate exchange connectivity
- send orders
- return exchange responses

CPU:

```text
CPU3
```

Queue:

```cpp
gateway_to_oms
```

---

### Why SPSC Queues

The topology is:

```text
single producer
single consumer
```

Examples:

```text
market data thread
        ↓
strategy thread
```

Only one producer.

Only one consumer.

SPSC queues are ideal:

Advantages:

- no locks
- no mutexes
- no heap allocation
- minimal cache contention
- deterministic throughput

Queue capacity remains bounded:

```cpp
constexpr BUS_CAPACITY = 1 << 14;
```

This prevents hidden latency accumulation.

---

### Runtime Verification

I verified runtime thread pinning using:

```bash
ps -L -o pid,tid,psr,comm -p <PID>
```

Observed output:

```text
PID      TID     PSR   COMMAND

73245    73245    0    threaded_runtim
73245    73246    0    llt-md
73245    73247    1    llt-strategy
73245    73248    2    llt-oms
73245    73249    3    llt-gateway
```

Interpretation:

```text
PSR = CPU currently executing thread
```

Verified mapping:

```text
llt-md          -> CPU0
llt-strategy    -> CPU1
llt-oms         -> CPU2
llt-gateway     -> CPU3
```

This confirms Linux thread affinity works correctly.

---

### Runtime Sample Output

Observed output:

```json
{"ts_ns":4934941350559,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":4934941382926,"level":"INFO","component":"oms","message":"accepted signal and created order"}

{"ts_ns":4934952119531,"level":"INFO","component":"gateway","message":"sent order and produced exchange response"}

{"ts_ns":4934952919975,"level":"INFO","component":"oms","message":"processed gateway ack"}

{"ts_ns":4935067329600,"level":"INFO","component":"oms","message":"thread stopped"}

{"ts_ns":4935067358064,"level":"INFO","component":"strategy","message":"thread stopped"}

{"ts_ns":4935069320055,"level":"INFO","component":"gateway","message":"thread stopped"}

{"ts_ns":4935074919978,"level":"INFO","component":"market_data","message":"thread stopped"}

{"ts_ns":4948835690289,"level":"INFO","component":"threaded_runtime","message":"shutdown complete"}
```

This demonstrates:

1. strategy generated signals

2. OMS created orders

3. gateway processed requests

4. acknowledgements propagated back

5. clean thread shutdown sequence occurred

---

### Design Tradeoffs

Current design:

Advantages:

- deterministic thread ownership
- bounded memory
- cache locality
- explicit runtime topology
- no lock contention

Limitations:

- single process
- in-process transport
- no networking
- no persistence
- no replay


Phase 1 complete.

The next phase replaces in-process queues with transport.

---

### Phase 2: TCP Node Transport

---

# Phase 2: TCP Binary Transport Layer

Phase 1 established a deterministic multi-threaded local runtime.

However, all communication was still in-process:

```text
market data thread
    →
strategy thread
    →
OMS thread
    →
gateway thread
```

Messages crossed queues, but not process boundaries.

Real trading systems eventually separate components into:

- independent processes
- independent NUMA regions
- independent hosts
- independent racks

That means communication must cross a real transport layer.

Phase 2 introduces:

```text
real TCP transport
real sockets
binary framing
message serialization
checksums
framed reads/writes
explicit disconnect handling
```

The architecture now becomes:

```text
OMS Process
    |
    | TCP Binary Frames
    |
Gateway Process
```

Even though this phase still runs locally on:

```text
127.0.0.1
```

the transport boundary is now real.

---

# Why TCP Framing Matters

TCP is NOT a message protocol.

TCP is a byte stream.

This is one of the most important networking concepts in systems programming.

A call like:

```cpp
send(fd, buffer, 128, 0);
```

does NOT guarantee:

```text
receiver gets 128 bytes in one recv()
```

The receiver may get:

```text
7 bytes
then
41 bytes
then
80 bytes
```

or:

```text
128 bytes
```

or:

```text
3 bytes
then disconnect
```

TCP preserves ordering.

TCP does NOT preserve application-level message boundaries.

Therefore every real binary protocol needs:

```text
frame header
payload length
message type
sequence number
integrity check
```

This phase implements exactly that.

---

# Transport Design

We implement a custom binary framed transport.

Frame structure:

```text
+----------------------+
| TcpFrameHeader       |
+----------------------+
| Payload Bytes        |
+----------------------+
```

Header:

```cpp
struct TcpFrameHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;

    MsgType type;

    uint32_t payload_size;
    Sequence sequence;

    uint32_t checksum;
};
```

Payload:

```text
MarketDataUpdate
Signal
NewOrder
Ack
Reject
Fill
Heartbeat
RiskState
```

Each payload is fixed-size binary data.

---

# Why Binary Protocols?

We intentionally avoid:

```text
JSON
XML
protobuf
```

on the hot path.

Reasons:

## JSON Problems

JSON introduces:

- parsing overhead
- heap allocation
- variable-length fields
- unpredictable latency
- string processing cost

Example:

```json
{
  "price": 100.25
}
```

vs:

```cpp
int64_t price_ticks;
```

In low-latency systems we prefer:

```text
fixed-size binary structs
```

because they are:

- cache friendly
- predictable
- faster to serialize
- faster to parse
- allocation free

---

# Why Not Coroutines/Futures/Promises?

We explicitly avoid:

```text
std::future
std::promise
coroutines
```

for the trading transport path.

Reason:

The trading path wants:

```text
deterministic
explicit
minimal-overhead
```

behavior.

Coroutines are excellent for:

- structured async IO
- admin APIs
- replay readers
- telemetry services

but the hot path typically prefers:

```text
explicit sockets
explicit polling
explicit buffers
explicit queues
```

because:

- coroutine frames may allocate
- schedulers add hidden complexity
- suspension/resume introduces overhead
- debugging becomes harder
- tail latency becomes less predictable

The current design uses:

```text
blocking send_exact()
blocking recv_exact()
fixed binary frames
```

This keeps behavior extremely explicit.

---

# send_exact()

One of the most important networking primitives.

Implementation:

```cpp
bool send_exact(...)
```

Purpose:

Ensure the ENTIRE buffer is transmitted.

Why?

Because:

```cpp
send(fd, data, 1024, 0);
```

may only send:

```text
412 bytes
```

The remaining bytes still need transmission.

Therefore:

```cpp
while (sent < len)
```

loop is required.

---

# recv_exact()

Equivalent receive primitive.

Implementation:

```cpp
bool recv_exact(...)
```

Purpose:

Ensure the ENTIRE frame is received.

Without this, the receiver may parse incomplete frames.

Example:

```text
expected frame = 128 bytes

recv() returns:
  first call  = 17 bytes
  second call = 44 bytes
  third call  = 67 bytes
```

Only after all bytes arrive can the frame be parsed safely.

---

# Checksums

Each payload includes:

```cpp
checksum_bytes(...)
```

Purpose:

Detect:

- corrupted frames
- malformed payloads
- truncated messages
- wrong payload interpretation

Current implementation uses:

```text
FNV-1a style hash
```

This is lightweight and fast.

Not cryptographic.

Only integrity-oriented.

---

# Message Flow

Phase 2 demo topology:

```text
OMS Client Thread
    |
    | TCP NewOrder Frame
    |
Gateway Server Thread
    |
    | TCP Ack Frame
    |
OMS Client Thread
```

Flow:

```text
OMS
  →
NewOrder
  →
TCP send
  →
Gateway
  →
Ack
  →
TCP send
  →
OMS
```

---

# Process Topology

Current topology:

```text
Single Linux Process
    ├── OMS Thread
    └── Gateway Thread
```

But transport is real:

```text
TCP socket
loopback interface
binary framing
```

This allows Phase 3+ to evolve naturally into:

```text
OMS process
Gateway process
Separate machines
```

without redesigning protocol logic.

---

# CPU Pinning

Threads remain pinned.

Observed runtime:

```text
tcp-oms      -> CPU2
tcp-gateway  -> CPU3
```

This preserves deterministic scheduling behavior.

---

# Runtime Verification

Observed output:

```text
aditya@singhm4  distributed-low-latency-trading-platform main!? ➜ tail -n 30 logs/tcp_transport_demo.log

{"ts_ns":7867954143309,"level":"INFO","component":"tcp_demo","message":"starting phase 2 TCP transport demo"}

{"ts_ns":7867954266991,"level":"INFO","component":"threading","message":"thread pinned to requested CPU core"}

{"ts_ns":7867954291697,"level":"INFO","component":"tcp_server","message":"listening on loopback TCP port"}

{"ts_ns":7867955022666,"level":"INFO","component":"threading","message":"thread pinned to requested CPU core"}

{"ts_ns":7867955164137,"level":"INFO","component":"tcp_client","message":"connected to TCP server"}

{"ts_ns":7867955208557,"level":"INFO","component":"tcp_oms","message":"sent NewOrder over TCP"}

{"ts_ns":7867955307994,"level":"INFO","component":"tcp_server","message":"accepted TCP client"}

{"ts_ns":7867955314394,"level":"INFO","component":"tcp_gateway","message":"received NewOrder over TCP"}

{"ts_ns":7867955355402,"level":"INFO","component":"tcp_gateway","message":"sent Ack/Reject over TCP"}

{"ts_ns":7867955356863,"level":"INFO","component":"tcp_oms","message":"received Ack over TCP"}

{"ts_ns":7867956620633,"level":"INFO","component":"tcp_demo","message":"phase 2 TCP transport demo complete"}

{"ts_ns":7867957212773,"level":"INFO","component":"logger","message":"logger stopped; dropped_logs=0"}
```

---

## What This Phase Demonstrates

Phase 2 demonstrates:

- real TCP transport
- framed binary protocol
- fixed-size payloads
- send_exact / recv_exact
- explicit serialization boundaries
- checksum validation
- OMS-to-gateway communication
- real socket lifecycle
- loopback networking
- deterministic framed messaging
- CPU-pinned networking threads
- async file logging

---

### Important Design Decisions

### Why Loopback TCP?

Using:

```text
127.0.0.1
```

still exercises:

- kernel socket stack
- framing logic
- connection establishment
- buffering
- send/recv behavior

without introducing external network variability.

This is ideal for incremental development.

---

### Why Explicit Framing Instead of Delimiters?

We avoid:

```text
newline-delimited protocols
```

because trading systems need:

- deterministic parsing
- fixed offsets
- fixed binary layout
- minimal parsing cost

Length-prefixed binary framing is the standard approach.

---

### Why Not UDP Yet?

UDP market data often comes BEFORE order transport.

Gateway order flow is usually:

```text
TCP
FIX
binary TCP
```

because:

- ordering matters
- reliability matters
- acknowledgements matter

UDP becomes more important later for:

```text
market data feed handlers
```

---

## Current Limitations

Still missing:

- non-blocking sockets
- epoll
- socket buffer tuning
- zero-copy IO
- kernel bypass
- retransmission metrics
- sequence gap recovery
- replay integration

These come later.

## Runtime Verification

Observed output after adding:

- TCP binary framing
- checksums
- sequence numbers
- reconnect handling
- heartbeats
- async logging
- CPU-pinned networking threads
- TCP_NODELAY
- transport liveness monitoring

Runtime output:

```text
aditya@singhm4  distributed-low-latency-trading-platform main!? ➜ tail -n 50 logs/tcp_transport_demo.log

{"ts_ns":7867954143309,"level":"INFO","component":"tcp_demo","message":"starting phase 2 TCP transport demo"}

{"ts_ns":7867954266991,"level":"INFO","component":"threading","message":"thread pinned to requested CPU core"}

{"ts_ns":7867954291697,"level":"INFO","component":"tcp_server","message":"listening on loopback TCP port"}

{"ts_ns":7867955022666,"level":"INFO","component":"threading","message":"thread pinned to requested CPU core"}

{"ts_ns":7867955164137,"level":"INFO","component":"tcp_client","message":"connected to TCP server"}

{"ts_ns":7867955208557,"level":"INFO","component":"tcp_oms","message":"sent NewOrder over TCP"}

{"ts_ns":7867955307994,"level":"INFO","component":"tcp_server","message":"accepted TCP client"}

{"ts_ns":7867955314394,"level":"INFO","component":"tcp_gateway","message":"received NewOrder over TCP"}

{"ts_ns":7867955355402,"level":"INFO","component":"tcp_gateway","message":"sent Ack/Reject over TCP"}

{"ts_ns":7867955356863,"level":"INFO","component":"tcp_oms","message":"received Ack over TCP"}

{"ts_ns":7867956620633,"level":"INFO","component":"tcp_demo","message":"phase 2 TCP transport demo complete"}

{"ts_ns":7867957212773,"level":"INFO","component":"logger","message":"logger stopped; dropped_logs=0"}

{"ts_ns":8772947576922,"level":"INFO","component":"tcp_demo","message":"starting phase 2 TCP transport demo"}

{"ts_ns":8772947655981,"level":"INFO","component":"threading","message":"thread pinned to requested CPU core"}

{"ts_ns":8772947671805,"level":"INFO","component":"threading","message":"thread pinned to requested CPU core"}

{"ts_ns":8772947677747,"level":"INFO","component":"tcp_server","message":"listening on loopback TCP port"}

{"ts_ns":8772948911131,"level":"INFO","component":"tcp_client","message":"connected to TCP server"}

{"ts_ns":8772948920798,"level":"INFO","component":"tcp_server","message":"accepted TCP client"}

{"ts_ns":8772949021842,"level":"INFO","component":"tcp_oms","message":"sent NewOrder over TCP"}

{"ts_ns":8772949024612,"level":"INFO","component":"tcp_gateway","message":"received NewOrder over TCP"}

{"ts_ns":8772949057810,"level":"INFO","component":"threading","message":"thread pinned to requested CPU core"}

{"ts_ns":8772949066380,"level":"INFO","component":"tcp_gateway","message":"sent Ack/Reject over TCP"}

{"ts_ns":8772949106055,"level":"INFO","component":"heartbeat","message":"sent heartbeat"}

{"ts_ns":8772949111663,"level":"INFO","component":"tcp_oms","message":"received Ack over TCP"}

{"ts_ns":8772949120868,"level":"INFO","component":"tcp_gateway","message":"received heartbeat"}

{"ts_ns":8773949596841,"level":"INFO","component":"tcp_demo","message":"phase 2 TCP transport demo complete"}

{"ts_ns":8773950407539,"level":"INFO","component":"logger","message":"logger stopped; dropped_logs=0"}
```

---

## What This Runtime Output Proves

The runtime verifies:

### 1. TCP Server Initialization

```text
tcp_server listening on loopback TCP port
```

The gateway transport layer successfully bound and listened on:

```text
127.0.0.1:<PORT>
```

---

### 2. OMS-to-Gateway TCP Connectivity

```text
tcp_client connected to TCP server
tcp_server accepted TCP client
```

This confirms:

- TCP socket creation
- loopback connection establishment
- server-side accept()
- client/server transport lifecycle

---

### 3. Binary Framed Message Transmission

```text
tcp_oms sent NewOrder over TCP
tcp_gateway received NewOrder over TCP
```

This proves:

- binary frame serialization
- frame header parsing
- payload size validation
- recv_exact() correctness
- send_exact() correctness

---

### 4. Exchange Response Path

```text
tcp_gateway sent Ack/Reject over TCP
tcp_oms received Ack over TCP
```

This confirms:

- bidirectional framed messaging
- OMS response handling
- deterministic request/response transport

---

### 5. Heartbeat Liveness Protocol

```text
heartbeat sent heartbeat
tcp_gateway received heartbeat
```

This proves:

- transport-level heartbeat frames
- peer liveness signaling
- heartbeat serialization/deserialization
- periodic transport activity

This becomes critical later for:

- stale peer detection
- network partition handling
- reconnect decisions
- fail-closed behavior

---

### 6. CPU Affinity Verification

```text
thread pinned to requested CPU core
```

Transport threads remain CPU-pinned.

This minimizes:

- scheduler migration
- cache invalidation
- tail latency jitter

---

### 7. Async Logger Stability

```text
logger stopped; dropped_logs=0
```

This confirms:

- async logger drained successfully
- bounded logger queue did not overflow
- no log backpressure occurred during runtime

---

## Distributed Systems Properties Achieved

At the end of Phase 2, the platform now supports:

| Capability | Status |
|---|---|
| Multi-threaded runtime 
| CPU affinity 
| Binary TCP framing 
| Checksums 
| Sequence numbers 
| Async logging 
| Heartbeats 
| Reconnect framework 
| Backpressure policy framework 
| TCP_NODELAY 
| Deterministic message transport 

Phase 2 complete.

---

### Phase 3: Persistent OMS Journal

The OMS should persist order intent before sending to the gateway.

This allows recovery after crash.

The journal should contain:

- New order intent
- Cancel intent
- Ack
- Reject
- Fill
- Cancel confirmation

---

### Phase 4: Real Market Data Adapter

Add a real market data connector.

Possible starting points:

- Binance book ticker WebSocket
- Coinbase WebSocket
- Polygon.io equities stream
- Simulated ITCH-style feed

Normalize external feed messages into `MarketDataUpdate`.

---

### Phase 5: Real Exchange Gateway

Add an exchange gateway.

Possible protocols:

- REST for simple testing
- WebSocket for crypto venues
- FIX for traditional venues

The gateway should convert internal `NewOrder` messages into exchange-native messages.

---

### Phase 6: Deterministic Replay

Persist all inbound market data and all strategy/OMS decisions.

Then add a replay binary:

```bash
./build/replay --input journal.bin
```

The replay should produce the same order decisions as the live system.

This is one of the strongest signals of deterministic design.

---

### Phase 7: Metrics and Observability

Add counters for:

- Market data messages received
- Signals generated
- Orders sent
- Orders rejected
- Acks received
- Fills received
- Queue drops
- Heartbeat misses
- Gateway disconnects
- Tail latency percentiles

---

## Sample Output

```bash
aditya@singhm4  systems/distributed_systems/distributed-low-latency-trading-platform 1m27s ➜ ./build/simulator
{"ts_ns":2274645598103,"level":"INFO","component":"simulator","message":"starting full local pipeline simulation"}
{"ts_ns":2274648927766,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274649208616,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274649219604,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274669298546,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274669317045,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274669337580,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274689463562,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274689571555,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274689617041,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274709730727,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274709760226,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274709776749,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274729862019,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274729910707,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274729928119,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274750054000,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274750078630,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274750089636,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274770201374,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274770217878,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274770221049,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274790293813,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274790316695,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274790319746,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274810448388,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274810505764,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274810515837,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274830639530,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274830700003,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274830715845,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274850804951,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274850880854,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274850897327,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274870993090,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274871034501,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274871045560,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274891316181,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274891559864,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274891574780,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274911658482,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274911719161,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274911740992,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274931834167,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274931885778,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274931909273,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274951997299,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274952034772,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274952043166,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274972148887,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274972216398,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274972280300,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2274992447479,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2274992470897,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2274992477345,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275012566771,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2275012595971,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2275012603466,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275032715222,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2275032871181,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2275032896272,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275053018066,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2275053037601,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2275053041209,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275073116646,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2275073140020,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2275073150341,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275093309065,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2275094518371,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2275094538133,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275114626495,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2275114649834,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2275114656071,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275134737609,"level":"INFO","component":"strategy","message":"generated signal"}
{"ts_ns":2275134771786,"level":"INFO","component":"oms","message":"accepted signal and created new order"}
{"ts_ns":2275134776405,"level":"INFO","component":"gateway","message":"order acked by simulated exchange"}
{"ts_ns":2275154886798,"level":"INFO","component":"simulator","message":"simulation complete"}
```

---

## Why This Is Relevant to HFT

HFT systems care about:

- Predictable latency
- Deterministic state transitions
- Fast market data ingestion
- Low-overhead strategy execution
- Safe order routing
- Fault isolation
- Burst handling
- Replayability
- Hardware-aware design

This project touches all of those areas.

It gives a clean base that can be extended into a realistic trading infrastructure project.

---

## License

MIT