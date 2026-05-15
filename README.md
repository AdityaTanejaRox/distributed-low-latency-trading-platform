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
- Real sockets
- Real exchange protocol
- Persistent journal
- Snapshot recovery
- Multi-venue routing
- CPU pinning
- NUMA placement
- Real historical replay
- Prometheus metrics
- Production-grade logging

---

## Extensions

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

#### Why Thread Pinning Matters

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

#### Thread Naming

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

#### Thread Responsibilities

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

#### Why SPSC Queues

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

#### Runtime Verification

Verified runtime thread pinning using:

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

#### Runtime Sample Output

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

#### Design Tradeoffs

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

## Phase 2: TCP Binary Transport Layer

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

#### Why TCP Framing Matters

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

#### Transport Design

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

#### Why Binary Protocols?

We intentionally avoid:

```text
JSON
XML
protobuf
```

on the hot path.

Reasons:

#### JSON Problems

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

#### Why Not Coroutines/Futures/Promises?

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

### send_exact()

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

### recv_exact()

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

#### Checksums

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

#### Message Flow

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

#### Process Topology

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

#### CPU Pinning

Threads remain pinned.

Observed runtime:

```text
tcp-oms      -> CPU2
tcp-gateway  -> CPU3
```

This preserves deterministic scheduling behavior.

---

#### Runtime Verification

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

#### What This Phase Demonstrates

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

#### Important Design Decisions

#### Why Loopback TCP?

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

#### Why Explicit Framing Instead of Delimiters?

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

#### Why Not UDP Yet?

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

#### Current Limitations

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

#### Runtime Verification

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

#### What This Runtime Output Proves

The runtime verifies:

#### 1. TCP Server Initialization

```text
tcp_server listening on loopback TCP port
```

The gateway transport layer successfully bound and listened on:

```text
127.0.0.1:<PORT>
```

---

#### 2. OMS-to-Gateway TCP Connectivity

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

#### 3. Binary Framed Message Transmission

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

#### 4. Exchange Response Path

```text
tcp_gateway sent Ack/Reject over TCP
tcp_oms received Ack over TCP
```

This confirms:

- bidirectional framed messaging
- OMS response handling
- deterministic request/response transport

---

#### 5. Heartbeat Liveness Protocol

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

#### 6. CPU Affinity Verification

```text
thread pinned to requested CPU core
```

Transport threads remain CPU-pinned.

This minimizes:

- scheduler migration
- cache invalidation
- tail latency jitter

---

#### 7. Async Logger Stability

```text
logger stopped; dropped_logs=0
```

This confirms:

- async logger drained successfully
- bounded logger queue did not overflow
- no log backpressure occurred during runtime

---

#### Distributed Systems Properties Achieved

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

## Phase 3: Persistent OMS Journal

#### Runtime Verification

Observed runtime:

```text
aditya@singhm4  distributed-low-latency-trading-platform main!? 1m55s ➜ ./build/journal_demo

aditya@singhm4  distributed-low-latency-trading-platform main!? ➜ tail -n 50 logs/journal_demo.log

{"ts_ns":10390751259054,"level":"INFO","component":"journal_demo","message":"starting phase 3 journal demo"}

{"ts_ns":10390758251786,"level":"INFO","component":"journal","message":"opened journal file"}

{"ts_ns":10390758300127,"level":"INFO","component":"journal","message":"appended journal record"}

{"ts_ns":10390758300730,"level":"INFO","component":"journal_demo","message":"journaled order intent before gateway send"}

{"ts_ns":10390758320185,"level":"INFO","component":"journal","message":"appended journal record"}

{"ts_ns":10390758320416,"level":"INFO","component":"journal_demo","message":"journaled gateway ack"}

{"ts_ns":10390758340251,"level":"INFO","component":"journal","message":"closed journal file"}

{"ts_ns":10390758562757,"level":"INFO","component":"journal_demo","message":"OrderIntent"}

{"ts_ns":10390758563067,"level":"INFO","component":"journal_demo","message":"GatewayAck"}

{"ts_ns":10390758563209,"level":"INFO","component":"journal_demo","message":"phase 3 journal demo complete"}

{"ts_ns":10390759320550,"level":"INFO","component":"logger","message":"logger stopped; dropped_logs=0"}

aditya@singhm4  distributed-low-latency-trading-platform main!? ➜ ls -lh journals

total 5.0K
-rw-rw-r-- 1 aditya aditya 204 May 14 12:15 oms_journal_demo.bin
```

---

#### What This Runtime Output Proves

Phase 3 proves that the OMS now has a durable recovery boundary.

This is one of the most important architectural transitions in the entire system.

The platform is no longer purely in-memory.

The OMS can now persist trading intent before external side effects occur.

---

#### OMS Durability Boundary

The most important event in the runtime:

```text
journaled order intent before gateway send
```

This is critical.

The sequence is now:

```text
strategy signal
    →
OMS creates NewOrder
    →
OMS journals OrderIntent
    →
gateway send occurs
```

NOT:

```text
gateway send
    →
journal later
```

Why?

Because if the process crashes after gateway send but before journaling:

```text
exchange may have live order
OMS forgets order existed
```

This creates catastrophic recovery ambiguity.

The correct ordering is:

```text
persist intent first
external side effect second
```

This is one of the fundamental principles of reliable trading infrastructure.

---

#### Journal Lifecycle Verification

Observed lifecycle:

```text
opened journal file
appended journal record
closed journal file
```

This confirms:

- journal initialization
- append-only binary writes
- durable flush path
- clean shutdown behavior

---

#### Binary Record Persistence

Observed:

```text
appended journal record
```

twice.

This corresponds to:

```text
OrderIntent
GatewayAck
```

The binary journal now stores:

```text
1. OMS trading intent
2. exchange acknowledgement
```

This creates the foundation for:

- deterministic replay
- crash recovery
- post-trade audit
- order lifecycle reconstruction
- compliance reconstruction
- exchange reconciliation

---

#### Journal Replay Verification

Observed:

```text
OrderIntent
GatewayAck
```

These messages were NOT generated live.

They were reconstructed by:

```cpp
JournalReader::read_all()
```

which:

1. opened the binary journal
2. parsed framed records
3. validated checksums
4. reconstructed Envelope payloads
5. replayed journal state

This proves:

```text
persisted OMS state can be recovered
```

which is one of the defining characteristics of a real trading platform.

---

#### Journal File Verification

Observed:

```text
-rw-rw-r-- 1 aditya aditya 204 May 14 12:15 oms_journal_demo.bin
```

This confirms:

- binary journal file creation
- persistent filesystem durability
- compact binary encoding

The journal currently occupies:

```text
204 bytes
```

because payloads are fixed-size binary structs rather than verbose text formats.

---

#### Why Append-Only Journals Matter

We intentionally use:

```text
append-only binary log
```

instead of:

```text
mutable database rows
```

Append-only logs are easier to reason about during failure recovery.

Advantages:

- deterministic ordering
- immutable history
- sequential disk writes
- easier replay
- easier corruption detection
- lower write amplification

Most serious trading systems internally rely on some form of:

```text
append-only event journal
```

even if hidden behind higher-level abstractions.

---

#### Checksums

Each journal record contains:

```cpp
checksum
```

Observed behavior:

```text
JournalReader validates checksum before replay
```

This detects:

- partial writes
- corrupted records
- truncated files
- invalid payload interpretation

The reader stops replay at the first invalid record.

This is important because crashes can occur mid-write.

---

#### Current Journal Tradeoffs

Current implementation:

```text
flush after every append
```

Advantages:

- strong durability
- simple correctness model
- easy recovery semantics

Tradeoff:

```text
higher latency
```

Production systems usually evolve toward:

- batched flushes
- dedicated journal threads
- mmap journals
- O_DIRECT
- replication
- NVMe optimized writes
- battery-backed write caches

But the current design is architecturally correct.

Correctness first.

Optimization later.

---

#### Distributed Systems Properties Achieved

At the end of Phase 3, the platform now supports:

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
| Backpressure framework 
| Persistent OMS journal 
| Binary replayable records 
| Append-only durability 
| Deterministic recovery foundation 

The system is now beginning to resemble a true trading infrastructure platform instead of a pure messaging demo.

---

#### Why Phase 3 Matters So Much

Phase 1 introduced:

```text
runtime topology
```

Phase 2 introduced:

```text
distributed transport
```

Phase 3 introduces:

```text
durability
```

Durability is what transforms a transient runtime into a recoverable system.

Without durability:

```text
process crash = state loss
```

With journaling:

```text
process crash != information loss
```

That distinction is enormous in trading systems.

---

Phase 3 complete.

---


## Phase 4: Real Market Data Adapter and Live Streaming Pipeline

Phase 4 moves the platform from synthetic market data into live market data ingestion.

This phase has two parts:

```text
Phase 4A: Venue normalization layer
Phase 4B: Live market data connectors
```

The goal is:

```text
external venue message
    →
venue-specific parser
    →
normalized MarketDataUpdate
    →
internal TopOfBook
    →
strategy pipeline
```

This is a major step because the strategy layer no longer depends on fake hand-written market data.

The system can now ingest real venue feeds and normalize them into the same internal representation.

---

#### Phase 4A: Normalization Layer

Different venues expose different message formats.

Examples:

```text
Binance bookTicker
Coinbase ticker
Hyperliquid l2Book
ITCH-style quote feed
```

Each venue has different:

- field names
- symbol conventions
- sequence models
- price formats
- quantity formats
- channel names
- payload nesting

The purpose of the normalization layer is to hide those differences from the rest of the platform.

The strategy should not care whether the data came from:

```text
Binance
Coinbase
Hyperliquid
Simulated ITCH
```

The strategy only consumes:

```cpp
MarketDataUpdate
```

---

#### Normalized Internal Format

All venue feeds normalize into:

```cpp
struct MarketDataUpdate {
    MessageHeader header;

    SymbolId symbol_id;
    Symbol symbol;

    Price bid_px;
    Quantity bid_qty;

    Price ask_px;
    Quantity ask_qty;

    Sequence exchange_sequence;
};
```

This gives the rest of the system one stable internal market data format.

---

#### Why Normalize Into Integer Prices?

External venues usually send prices as strings or decimals:

```json
"81531.83"
```

The internal system stores prices as integer ticks:

```text
8153183
```

Example:

```text
81531.83 dollars
    →
8153183 cents/ticks
```

Reasons:

- avoid floating point rounding
- deterministic comparisons
- faster arithmetic
- stable serialization
- easier replay
- safer order price construction

Trading systems generally avoid floating point in the order path.

---

#### Why Normalize Quantities Into Integer Units?

Crypto venues often send fractional quantities:

```json
"0.243030"
```

The platform normalizes this to integer micro-units:

```text
243030
```

This preserves fractional sizes while keeping internal math integer-only.

Example:

```text
0.243030 BTC
    →
243030 micro-units
```

This fixed the earlier issue where fractional crypto sizes were truncating to zero.

---

### Phase 4B: Live Market Data Connectors

Phase 4B adds live connectors for:

```text
1. Binance live bookTicker WebSocket
2. Coinbase live ticker WebSocket
3. Hyperliquid live l2Book WebSocket
4. Simulated ITCH-style TCP feed generator
```

The external WebSocket boundary uses Boost.Beast.

After the raw venue frame is received, the system immediately converts it into internal `MarketDataUpdate`.

---

#### Why Boost.Beast?

In Go and Rust, WebSocket support usually comes from common ecosystem packages.

In C++, raw POSIX sockets are not enough for `wss://` feeds.

A `wss://` connection requires:

- TCP connection
- TLS handshake
- SNI hostname setup
- HTTP upgrade handshake
- WebSocket framing
- masking
- ping/pong handling
- close frame handling
- message fragmentation handling

Boost.Beast handles the external WebSocket protocol correctly.

The project still keeps the internal trading system deterministic:

```text
Boost.Beast WebSocket
    →
raw JSON text
    →
venue parser
    →
MarketDataUpdate
    →
internal queues/order book/strategy
```

Boost.Beast is only used at the external venue boundary.

---

#### Supported Live Venues

### Binance

Connector:

```text
binance_live_md
```

Stream:

```text
wss://data-stream.binance.vision/ws/btcusdt@bookTicker
```

Normalized fields:

```text
s  -> symbol
u  -> exchange sequence
b  -> best bid price
B  -> best bid quantity
a  -> best ask price
A  -> best ask quantity
```

Output shape:

```text
venue=Binance symbol=BTCUSDT seq=... bid=... bid_qty=... ask=... ask_qty=...
```

---

#### Coinbase

Connector:

```text
coinbase_live_md
```

Stream:

```text
wss://ws-feed.exchange.coinbase.com
```

Subscription:

```json
{
  "type": "subscribe",
  "product_ids": ["BTC-USD"],
  "channels": ["ticker"]
}
```

Normalized fields:

```text
product_id       -> symbol
sequence         -> exchange sequence
best_bid         -> best bid price
best_bid_size    -> best bid quantity
best_ask         -> best ask price
best_ask_size    -> best ask quantity
```

Output shape:

```text
venue=Coinbase symbol=BTC-USD seq=... bid=... bid_qty=... ask=... ask_qty=...
```

---

#### Hyperliquid

Connector:

```text
hyperliquid_live_md
```

Stream:

```text
wss://api.hyperliquid.xyz/ws
```

Subscription:

```json
{
  "method": "subscribe",
  "subscription": {
    "type": "l2Book",
    "coin": "BTC"
  }
}
```

Normalized top-of-book extraction:

```text
levels[0][0] -> best bid
levels[1][0] -> best ask
```

Normalized fields:

```text
coin          -> symbol
time          -> exchange sequence / event time
bid px        -> best bid price
bid sz        -> best bid quantity
ask px        -> best ask price
ask sz        -> best ask quantity
```

Output shape:

```text
venue=Hyperliquid symbol=BTC seq=... bid=... bid_qty=... ask=... ask_qty=...
```

---

#### Simulated ITCH-Style Feed

Live public ITCH feeds are usually not freely available without exchange data access.

So this project includes a local TCP generator that emits ITCH-style quote lines.

Generator:

```text
itch_feed_generator
```

Consumer:

```text
itch_live_md
```

Format:

```text
Q|sequence|symbol|bid_px|bid_qty|ask_px|ask_qty
```

Example:

```text
Q|1000|MSFT|42125|300|42126|400
```

Output shape:

```text
venue=SimulatedItch symbol=MSFT seq=1000 bid=42125 ask=42126
```

This allows the project to model sequence-heavy market data ingestion without requiring paid exchange data.

---

#### Continuous Streaming Mode

Connectors now support:

```text
max_updates = 0
```

Meaning:

```text
stream forever until Ctrl+C or disconnect
```

This was important because a real market data connector should not stop after five messages.

The connector binaries now continuously stream normalized updates.

---

#### Live Runtime Architecture

Phase 4 also adds a live runtime:

```text
live_market_data_runtime
```

This connects live market data into the internal platform pipeline:

```text
Live WebSocket Feed
    →
Venue Normalizer
    →
TopOfBook
    →
MarketDataUpdate Queue
    →
StrategyEngine
    →
OMS
    →
Simulated Gateway
    →
Ack/Reject back to OMS
```

This proves the platform now has a real streaming data path.

Phase 5 will replace the simulated gateway with testnet order submission.

---

#### How to Run

Build:

```bash
cmake --build build -j
```

---

#### Run Binance Live Connector

```bash
./build/binance_live_md
```

Stop:

```bash
Ctrl+C
```

---

#### Run Coinbase Live Connector

```bash
./build/coinbase_live_md
```

Stop:

```bash
Ctrl+C
```

---

#### Run Hyperliquid Live Connector

```bash
./build/hyperliquid_live_md
```

Stop:

```bash
Ctrl+C
```

---

#### Run Simulated ITCH Feed

Use two terminals.

Terminal 1:

```bash
./build/itch_feed_generator
```

Terminal 2:

```bash
./build/itch_live_md
```

---

#### Run Full Live Market Data Runtime

Coinbase:

```bash
./build/live_market_data_runtime coinbase
```

Hyperliquid:

```bash
./build/live_market_data_runtime hyperliquid
```

Binance:

```bash
./build/live_market_data_runtime binance
```

Stop:

```bash
Ctrl+C
```

---

#### Runtime Verification: Hyperliquid

Observed output:

```text
aditya@singhm4  distributed-low-latency-trading-platform main!? 3s ➜ ./build/hyperliquid_live_md

venue=Hyperliquid symbol=BTC seq=1778786785318 bid=8140300 bid_qty=647340 ask=8140400 ask_qty=42018620
venue=Hyperliquid symbol=BTC seq=1778786785848 bid=8140300 bid_qty=647710 ask=8140400 ask_qty=33940390
venue=Hyperliquid symbol=BTC seq=1778786786395 bid=8140300 bid_qty=647710 ask=8140400 ask_qty=32701640
venue=Hyperliquid symbol=BTC seq=1778786786937 bid=8140300 bid_qty=647480 ask=8140400 ask_qty=32539490
venue=Hyperliquid symbol=BTC seq=1778786787442 bid=8140300 bid_qty=991420 ask=8140400 ask_qty=32554760
venue=Hyperliquid symbol=BTC seq=1778786787974 bid=8140300 bid_qty=991750 ask=8140400 ask_qty=32543800
venue=Hyperliquid symbol=BTC seq=1778786788523 bid=8140300 bid_qty=1037390 ask=8140400 ask_qty=32543630
venue=Hyperliquid symbol=BTC seq=1778786789081 bid=8140300 bid_qty=1037390 ask=8140400 ask_qty=36842600
venue=Hyperliquid symbol=BTC seq=1778786789633 bid=8140300 bid_qty=1037200 ask=8140400 ask_qty=36016890
venue=Hyperliquid symbol=BTC seq=1778786790174 bid=8140100 bid_qty=170 ask=8140400 ask_qty=52539600
venue=Hyperliquid symbol=BTC seq=1778786790701 bid=8138600 bid_qty=6070 ask=8138700 ask_qty=8280500
venue=Hyperliquid symbol=BTC seq=1778786791267 bid=8138600 bid_qty=1343720 ask=8138700 ask_qty=20942960
venue=Hyperliquid symbol=BTC seq=1778786791802 bid=8138600 bid_qty=2405580 ask=8138700 ask_qty=16996600
venue=Hyperliquid symbol=BTC seq=1778786792361 bid=8138600 bid_qty=4082170 ask=8138700 ask_qty=15886580
venue=Hyperliquid symbol=BTC seq=1778786792893 bid=8138600 bid_qty=4161880 ask=8138700 ask_qty=14977440
venue=Hyperliquid symbol=BTC seq=1778786793440 bid=8138600 bid_qty=976580 ask=8138700 ask_qty=17806170
venue=Hyperliquid symbol=BTC seq=1778786794000 bid=8138600 bid_qty=4454390 ask=8138700 ask_qty=13494000
venue=Hyperliquid symbol=BTC seq=1778786794562 bid=8138600 bid_qty=4515900 ask=8138700 ask_qty=13495820
venue=Hyperliquid symbol=BTC seq=1778786795105 bid=8138600 bid_qty=4163510 ask=8138700 ask_qty=13248000
venue=Hyperliquid symbol=BTC seq=1778786795655 bid=8138600 bid_qty=4163510 ask=8138700 ask_qty=13248000
venue=Hyperliquid symbol=BTC seq=1778786796193 bid=8138600 bid_qty=4166340 ask=8138700 ask_qty=13235730
venue=Hyperliquid symbol=BTC seq=1778786796740 bid=8138600 bid_qty=4166340 ask=8138700 ask_qty=13111020
venue=Hyperliquid symbol=BTC seq=1778786797289 bid=8138600 bid_qty=4166340 ask=8138700 ask_qty=11743740
venue=Hyperliquid symbol=BTC seq=1778786797817 bid=8138600 bid_qty=1054210 ask=8138700 ask_qty=13135560
venue=Hyperliquid symbol=BTC seq=1778786798338 bid=8138600 bid_qty=4689320 ask=8138700 ask_qty=13135420
venue=Hyperliquid symbol=BTC seq=1778786798897 bid=8138600 bid_qty=3760260 ask=8138700 ask_qty=13126480
venue=Hyperliquid symbol=BTC seq=1778786799449 bid=8138600 bid_qty=3616450 ask=8138700 ask_qty=13124150
venue=Hyperliquid symbol=BTC seq=1778786799984 bid=8138600 bid_qty=1860250 ask=8138700 ask_qty=13124150
venue=Hyperliquid symbol=BTC seq=1778786800532 bid=8138600 bid_qty=2192880 ask=8138700 ask_qty=13124150
venue=Hyperliquid symbol=BTC seq=1778786801059 bid=8138600 bid_qty=2291860 ask=8138700 ask_qty=13124150
^C
```

This verifies:

- live Hyperliquid WebSocket connectivity
- l2Book subscription
- top-of-book extraction
- normalized price ticks
- normalized quantity micro-units
- continuous streaming

---

#### Runtime Verification: Coinbase

Observed output:

```text
venue=Coinbase symbol=BTC-USD seq=128173992890 bid=8146648 bid_qty=243030 ask=8146649 ask_qty=305656
venue=Coinbase symbol=BTC-USD seq=128173992893 bid=8146648 bid_qty=147 ask=8146649 ask_qty=305656
venue=Coinbase symbol=BTC-USD seq=128173992895 bid=8146648 bid_qty=16 ask=8146649 ask_qty=305656
venue=Coinbase symbol=BTC-USD seq=128173992897 bid=8145966 bid_qty=3879 ask=8146649 ask_qty=305656
venue=Coinbase symbol=BTC-USD seq=128173994132 bid=8145576 bid_qty=13 ask=8145577 ask_qty=74220
venue=Coinbase symbol=BTC-USD seq=128173994487 bid=8145576 bid_qty=27 ask=8145577 ask_qty=192452
venue=Coinbase symbol=BTC-USD seq=128173994489 bid=8145301 bid_qty=36847 ask=8145577 ask_qty=192452
venue=Coinbase symbol=BTC-USD seq=128173994491 bid=8145301 bid_qty=36689 ask=8145577 ask_qty=192452
venue=Coinbase symbol=BTC-USD seq=128173994581 bid=8145412 bid_qty=86759 ask=8145488 ask_qty=7360
venue=Coinbase symbol=BTC-USD seq=128173994965 bid=8145412 bid_qty=676353 ask=8145425 ask_qty=3680
venue=Coinbase symbol=BTC-USD seq=128173995187 bid=8145412 bid_qty=676369 ask=8145413 ask_qty=132329
venue=Coinbase symbol=BTC-USD seq=128173995388 bid=8145412 bid_qty=589610 ask=8145413 ask_qty=128360
venue=Coinbase symbol=BTC-USD seq=128173995539 bid=8145412 bid_qty=589610 ask=8145413 ask_qty=122538
venue=Coinbase symbol=BTC-USD seq=128173996117 bid=8145412 bid_qty=589610 ask=8145413 ask_qty=262538
venue=Coinbase symbol=BTC-USD seq=128173996378 bid=8145412 bid_qty=589610 ask=8145413 ask_qty=226760
venue=Coinbase symbol=BTC-USD seq=128173996722 bid=8145412 bid_qty=589610 ask=8145413 ask_qty=275559
venue=Coinbase symbol=BTC-USD seq=128173997048 bid=8145412 bid_qty=589571 ask=8145413 ask_qty=261538
venue=Coinbase symbol=BTC-USD seq=128173997050 bid=8145412 bid_qty=588690 ask=8145413 ask_qty=261538
venue=Coinbase symbol=BTC-USD seq=128173997169 bid=8145412 bid_qty=588729 ask=8145413 ask_qty=261538
venue=Coinbase symbol=BTC-USD seq=128173997496 bid=8145412 bid_qty=588729 ask=8145413 ask_qty=261538
venue=Coinbase symbol=BTC-USD seq=128173997498 bid=8145412 bid_qty=588729 ask=8145413 ask_qty=261420
venue=Coinbase symbol=BTC-USD seq=128173997652 bid=8145412 bid_qty=588729 ask=8145413 ask_qty=261420
venue=Coinbase symbol=BTC-USD seq=128173997702 bid=8145412 bid_qty=588729 ask=8145413 ask_qty=261355
venue=Coinbase symbol=BTC-USD seq=128173997832 bid=8145412 bid_qty=588729 ask=8145413 ask_qty=260579
venue=Coinbase symbol=BTC-USD seq=128173998097 bid=8145412 bid_qty=648667 ask=8145413 ask_qty=260579
venue=Coinbase symbol=BTC-USD seq=128173998114 bid=8145412 bid_qty=646867 ask=8145413 ask_qty=260579
venue=Coinbase symbol=BTC-USD seq=128173998259 bid=8145412 bid_qty=646867 ask=8145413 ask_qty=264871
venue=Coinbase symbol=BTC-USD seq=128173998313 bid=8145412 bid_qty=646867 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998363 bid=8145412 bid_qty=646866 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998366 bid=8145412 bid_qty=536480 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998375 bid=8145412 bid_qty=60055 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998377 bid=8145412 bid_qty=60039 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998379 bid=8145412 bid_qty=60000 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998381 bid=8145412 bid_qty=0 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998536 bid=8145412 bid_qty=589610 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998538 bid=8145412 bid_qty=589571 ask=8145413 ask_qty=264528
venue=Coinbase symbol=BTC-USD seq=128173998540 bid=8145412 bid_qty=589566 ask=8145413 ask_qty=264528
^C
```

This verifies:

- live Coinbase WebSocket connectivity
- ticker subscription
- bid/ask normalization
- fractional quantity scaling
- continuous streaming
- valid internal top-of-book representation

---

#### Runtime Verification: Binance

Observed behavior:

```text
Binance streamed continuously until Ctrl+C.
```

The Binance connector was fixed to use:

```text
wss://data-stream.binance.vision/ws/btcusdt@bookTicker
```

instead of the earlier endpoint that failed.

This verifies:

- Binance WebSocket connectivity
- public market-data-only endpoint usage
- continuous bookTicker stream
- normalized `MarketDataUpdate` flow

---

#### Runtime Verification: Simulated ITCH

Observed behavior:

```text
venue=SimulatedItch symbol=MSFT seq=1000 bid=42125 ask=42126
venue=SimulatedItch symbol=MSFT seq=1001 bid=42126 ask=42127
venue=SimulatedItch symbol=MSFT seq=1002 bid=42127 ask=42128
...
```

This verifies:

- local TCP feed generation
- sequence-heavy ITCH-style quote ingestion
- line framing
- normalization into `MarketDataUpdate`

---

#### What Phase 4 Proves

Phase 4 proves the system can now ingest and normalize real live market data.

The platform now supports:

| Venue | Feed Type | Status |
|---|---|---|
| Binance | Live WebSocket bookTicker | ✅ |
| Coinbase | Live WebSocket ticker | ✅ |
| Hyperliquid | Live WebSocket l2Book | ✅ |
| Simulated ITCH | Local TCP quote feed | ✅ |

It also proves:

- external venue feeds can be parsed
- venue data can be normalized
- normalized updates use integer prices
- normalized updates use integer quantities
- continuous streaming works
- the internal strategy pipeline can consume live market data
- feed handlers are now separated from strategy logic

---

#### Current Limitations

This phase uses top-of-book normalization.

It does not yet maintain a full depth order book.

For full depth, future extensions should add:

- per-price-level book maps
- sequence gap detection
- snapshot recovery
- incremental depth updates
- venue-specific order book reconstruction
- stale book detection
- cross-venue book aggregation

For Phase 4, top-of-book is sufficient because the current strategy consumes:

```text
best bid
best ask
spread
```

---

Phase 4 is complete.

---

## Phase 5: Real Exchange Gateway

Phase 5 introduces a real exchange gateway capable of authenticated order submission instead of simulated exchange responses.

Architecture:

```text
Strategy
    →
OMS
    →
Gateway
    →
Exchange API
```

The gateway converts internal `NewOrder` messages into venue-native requests and maps exchange responses back into internal ACK / Reject messages.

Implemented:

- HTTPS client using Boost.Beast + TLS
- authenticated REST request flow
- HMAC request signing
- environment-based API credentials
- exchange response handling
- internal-to-external order translation

Current implementation targets:

```text
Kraken Futures Demo
```

Environment variables:

```bash
export KRAKEN_FUTURES_DEMO_API_KEY="..."
export KRAKEN_FUTURES_DEMO_API_SECRET="..."
```

Run:

```bash
./build/kraken_futures_demo_order
```

Expected flow:

```text
internal NewOrder
      →
signed exchange request
      →
exchange ACK / Reject
      →
OMS update
```

This phase establishes the external exchange boundary and replaces mock execution with real authenticated venue connectivity.

---

## Phase 5.5: Distributed Node Deployment + Docker Runtime

Earlier phases ran the entire trading platform inside a single process:

```text
MarketData
    →
Strategy
    →
OMS
    →
Gateway
```

While useful for development, a single process does not provide:

- fault isolation
- process isolation
- independent deployment
- realistic distributed behavior
- node restarts
- network failure handling

Phase 5.5 converts the platform into independent deployable services.

Architecture:

```text
market_data_node
        |
        | TCP
        ↓
strategy_node
        |
        | TCP
        ↓
oms_node
        |
        | TCP
        ↓
gateway_node
```

Each node now runs as its own process and communicates using the TCP transport layer built earlier.

CPU pinning:

```text
market_data_node → core 0
strategy_node    → core 1
oms_node         → core 2
gateway_node     → core 3
```

Docker Compose was added so the entire distributed system can be launched with a single command rather than manually opening multiple terminals.

Run:

```bash
docker compose up --build
```

Shutdown:

```bash
docker compose down
```

Live market data continues to flow from Coinbase / Hyperliquid / Binance into the distributed pipeline.

Observed runtime flow:

```text
market_data_node
→ normalized live Coinbase ticker

strategy_node
→ sent Signal to OMS

oms_node
→ appended journal record
→ journaled and processed gateway Ack

gateway_node
→ processed NewOrder and returned Ack/Reject
```

### Runtime Verification

Observed distributed runtime logs:

```text
market_data_node
→ normalized live Coinbase ticker

strategy_node
→ sent Signal to OMS

oms_node
→ appended journal record
→ journaled and processed gateway Ack

gateway_node
→ processed NewOrder and returned Ack/Reject
```

Observed full pipeline:

```text
Live Market Data
    →
Strategy Signal
    →
OMS Risk + Journal
    →
Gateway
    →
Ack
```

This verifies:

- multi-process deployment
- TCP service-to-service communication
- live market data ingestion
- strategy signal generation
- OMS journaling
- gateway acknowledgements
- Docker-based orchestration

The platform now behaves as an actual distributed trading system rather than a single-process simulation.

---

## Phase 6: Distributed Deterministic Replay

Phase 6 adds distributed replay and audit reconstruction.

Each node writes its own replay file:

```text
market_data_node → journals/replay_market_data.bin
strategy_node    → journals/replay_strategy.bin
oms_node         → journals/replay_oms.bin
gateway_node     → journals/replay_gateway.bin
```

These files are then merged offline into one ordered event timeline:

```bash
./build/replay_merge journals/replay_merged.bin \
  journals/replay_market_data.bin \
  journals/replay_strategy.bin \
  journals/replay_oms.bin \
  journals/replay_gateway.bin
```

The merged file can be viewed with:

```bash
./build/replay_timeline journals/replay_merged.bin | head -200
```

Observed replay generation:

```text
Replay complete
events_read=356
market_data_events=356
signals_generated=108
orders_generated=108
acks_generated=108
rejects_generated=0
```

Observed distributed replay files:

```text
journals/replay_gateway.bin      4.8K
journals/replay_market_data.bin   11K
journals/replay_oms.bin          9.5K
journals/replay_strategy.bin     4.8K
```

Observed merge:

```text
merged_events=862
output=journals/replay_merged.bin
```

Sample merged timeline:

```text
34182055639146 | MarketData | seq=1 | MarketData symbol=BTC-USD bid=8092001 ask=8092057
34182055779846 | Strategy   | seq=1 | Signal side=BUY px=8092002 qty=1
34182069277029 | OMS        | seq=1 | NewOrder clOrdId=1 side=BUY px=8092002 qty=1
34182069470686 | Gateway    | seq=1 | Ack clOrdId=1 exchOrderId=1000
34182083778697 | OMS        | seq=2 | Ack clOrdId=1 exchOrderId=1000
```

This verifies:

- real live market data was captured
- each node recorded its own events
- replay logs were merged by timestamp and sequence
- full packet/event lifecycle reconstruction works
- strategy, OMS, gateway, and ACK flow can be audited after the run

Phase 6 provides the foundation for deterministic debugging, crash analysis, post-trade audit, and replay-based verification.

---

## Phase 7: Metrics and Observability

Phase 7 adds runtime metrics and observability across the distributed trading platform.

Each node writes metrics to:

```text
metrics/<node_name>.jsonl
```

Tracked counters include:

- market data messages received
- signals generated
- orders sent
- orders rejected
- ACKs received
- fills received
- queue drops
- heartbeat misses
- gateway disconnects
- journal writes
- replay events written/read
- latency percentiles

Metrics can be inspected with:

```bash
./build/metrics_report
```

Observed output:

```text
== metrics/market_data_node.jsonl ==
{"market_data_received":600,"signals_generated":0,"orders_sent":0,"orders_rejected":0,"acks_received":0,"fills_received":0,"queue_drops":0,"heartbeat_misses":0,"gateway_disconnects":0,"journal_writes":0,"replay_events_written":600,"replay_events_read":0,"latency_count":600,"latency_min_ns":220,"latency_p50_ns":685,"latency_p99_ns":2276,"latency_p999_ns":2685,"latency_max_ns":3032}

== metrics/strategy_node.jsonl ==
{"market_data_received":655,"signals_generated":397,"orders_sent":0,"orders_rejected":0,"acks_received":0,"fills_received":0,"queue_drops":0,"heartbeat_misses":0,"gateway_disconnects":0,"journal_writes":0,"replay_events_written":794,"replay_events_read":0,"latency_count":655,"latency_min_ns":53539,"latency_p50_ns":187429,"latency_p99_ns":2023483,"latency_p999_ns":3281919,"latency_max_ns":3392613}

== metrics/oms_node.jsonl ==
{"market_data_received":0,"signals_generated":0,"orders_sent":397,"orders_rejected":0,"acks_received":397,"fills_received":0,"queue_drops":0,"heartbeat_misses":0,"gateway_disconnects":0,"journal_writes":1588,"replay_events_written":1588,"replay_events_read":0,"latency_count":0,"latency_min_ns":0,"latency_p50_ns":0,"latency_p99_ns":0,"latency_p999_ns":0,"latency_max_ns":0}

== metrics/gateway_node.jsonl ==
{"market_data_received":0,"signals_generated":0,"orders_sent":397,"orders_rejected":0,"acks_received":397,"fills_received":0,"queue_drops":0,"heartbeat_misses":0,"gateway_disconnects":1,"journal_writes":0,"replay_events_written":794,"replay_events_read":0,"latency_count":0,"latency_min_ns":0,"latency_p50_ns":0,"latency_p99_ns":0,"latency_p999_ns":0,"latency_max_ns":0}
```

This verifies:

```text
Live Market Data
    →
Strategy Signals
    →
OMS Orders
    →
Gateway ACKs
    →
Metrics + Replay + Journal
```

Phase 7 completes the observability layer and makes the platform measurable instead of just functional.

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

---

## License

MIT