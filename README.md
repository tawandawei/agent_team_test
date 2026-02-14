# Agent Team Test

High-performance UDP peer-to-peer communication application with real-time
threading, lock-free data structures, and event-driven architecture.

## Table of Contents

- [Overview](#overview)
- [Application Architecture](#application-architecture)
  - [System Context](#system-context)
  - [Module Overview](#module-overview)
  - [Thread Model](#thread-model)
  - [Data Flow](#data-flow)
  - [Packet Format](#packet-format)
- [Project Structure](#project-structure)
- [Module Details](#module-details)
  - [app - Application Layer](#app---application-layer)
  - [event - Event Loop](#event---event-loop)
  - [socket - Network I/O](#socket---network-io)
  - [timer - Timer Management](#timer---timer-management)
  - [thread - Threading Infrastructure](#thread---threading-infrastructure)
  - [stats - Latency Benchmarking](#stats---latency-benchmarking)
- [Build](#build)
- [Usage](#usage)
- [Coding Conventions](#coding-conventions)
- [License](#license)

---

## Overview

Agent Team Test is a UDP peer-to-peer communication test application designed
for low-latency, high-throughput packet exchange between two nodes. Each node
transmits periodic packets containing a lifesign counter and monitors the
remote peer for communication loss or unstable timing.

Key characteristics:

- **Real-time threading** with SCHED_FIFO and CPU affinity
- **Lock-free SPSC ring buffers** for inter-thread communication
- **epoll-based event loop** for timer-driven operations
- **CRC32-protected** application packet format
- **Graceful shutdown** via POSIX signal handling (SIGINT / SIGTERM)

---

## Application Architecture

### System Context

```
┌─────────────────────┐         UDP          ┌─────────────────────┐
│                     │ ◄──────────────────► │                     │
│     Node A          │    Peer-to-Peer      │     Node B          │
│  (agent_team_test)  │    Communication     │  (agent_team_test)  │
│                     │                      │                     │
└─────────────────────┘                      └─────────────────────┘
     src:port A                                   src:port B
     dst:port B                                   dst:port A
```

Each node is an independent instance of the same binary. Node A's destination
is Node B's source, and vice versa.

### Module Overview

```
┌────────────────────────────────────────────────────────────────────┐
│                        main (Entry Point)                          │
│  - CLI argument parsing                                            │
│  - Object wiring & initialization                                  │
│  - Event loop lifecycle                                            │
├────────────────┬──────────────┬──────────────┬─────────────────────┤
│     app        │    event     │   socket     │   thread            │
│                │              │              │                     │
│ AppPacket      │ EventLoop    │ UdpNode      │ UdpThreadManager    │
│ ArgParser      │              │              │ LockFreeRingBuffer  │
│ SignalHandler  │              │              │                     │
├────────────────┴──────────────┴──────────────┴─────────────────────┤
│                         stats                                      │
│                LatencyStats    TerminalUI                          │
├────────────────────────────────────────────────────────────────────┤
│                              timer                                 │
│                           TimerHandle                              │
└────────────────────────────────────────────────────────────────────┘
```

### Thread Model

The application runs three execution contexts:

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Process                                      │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │  Main Thread                                                   │  │
│  │  - Event loop (epoll)                                          │  │
│  │  - TX timer callback  ──► encodes packet ──► TX Ring Buffer    │  │
│  │  - Comm monitor timer callback                                 │  │
│  │  - Signal handler check                                        │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌──────────────────────────┐    ┌──────────────────────────────┐    │
│  │  RX Thread               │    │  TX Thread                   │    │
│  │  CPU Core: 2             │    │  CPU Core: 3                 │    │
│  │  SCHED_FIFO, Prio: 80    │    │  SCHED_FIFO, Prio: 70        │    │
│  │  SIGINT/SIGTERM blocked  │    │  SIGINT/SIGTERM blocked      │    │
│  │                          │    │                              │    │
│  │  do {                    │    │  do {                        │    │
│  │    recvfrom(socket)      │    │    pop(TX Ring Buffer)       │    │
│  │    push(RX Ring Buffer)  │    │    sendto(socket)            │    │
│  │    invoke RX callback    │    │  } while (running)           │    │
│  │  } while (running)       │    │                              │    │
│  └──────────────────────────┘    └──────────────────────────────┘    │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

| Thread     | Role                | Scheduling   | Priority  | CPU Core  |
|:-----------|:--------------------|:-------------|:----------|:----------|
| Main       | Event loop, timers  | SCHED_OTHER  | default   | any       |
| RX Thread  | Socket receive      | SCHED_FIFO   | 80        | 2         |
| TX Thread  | Socket transmit     | SCHED_FIFO   | 70        | 3         |

### Data Flow

```
                    Transmit Path
                    ─────────────
  TX Timer ──► AppPacket::encode() ──► TX Ring Buffer ──► TX Thread ──► sendto()
  (100 ms)         (main thread)        (lock-free)      (SCHED_FIFO)   (kernel)


                    Receive Path
                    ────────────
  recvfrom() ──► RX Ring Buffer ──► RX Callback ──► AppPacket::decode()
  (kernel)       (RX Thread)        (RX Thread)      (RX Thread)
                  (lock-free)                         ├─ lifesign update
                                                      ├─ interval measurement
                                                      └─ stability check
```

### Packet Format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│                         unique_id (32-bit)                        │ Header
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│         lifesign (16-bit)         │       data_length (16-bit)    │
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│                                                                   │
│                       data (0..256 bytes)                         │ Payload
│                                                                   │
├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤
│                          crc32 (32-bit)                           │ Footer
└─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
```

- **unique_id**: Identifies the sending node
- **lifesign**: Auto-incremented counter per transmission
- **data_length**: Payload length in bytes (max 256)
- **data**: Application payload
- **crc32**: CRC32 over header + data

---

## Project Structure

```
agent_team_test/
├── CMakeLists.txt              # Build configuration (C++26, CMake 3.20+)
├── Makefile                    # Convenience targets: build, clean, run, test_node_1, test_node_2
├── LICENSE                     # MIT License
├── README.md                   # This file
│
├── include/                    # Public headers
│   ├── app/
│   │   ├── AppPacket.hpp       # Packet encode/decode, comm monitoring
│   │   ├── ArgParser.hpp       # CLI argument parsing
│   │   └── SignalHandler.hpp   # POSIX signal handler (singleton)
│   ├── event/
│   │   └── EventLoop.hpp       # epoll-based event loop
│   ├── socket/
│   │   └── UdpNode.hpp         # UDP socket wrapper
│   ├── thread/
│   │   ├── LockFreeRingBuffer.hpp  # SPSC lock-free ring buffer (template)
│   │   └── UdpThreadManager.hpp    # RX/TX thread lifecycle management
│   └── timer/
│       └── timer.hpp           # timerfd wrapper
│
├── include/                    # Public headers (continued)
│   └── stats/
│       ├── LatencyStats.hpp    # Percentile statistics (header-only template)
│       └── TerminalUI.hpp      # Split-screen ANSI terminal dashboard
│
├── src/                        # Implementation
│   ├── app/
│   │   ├── main.cpp            # Entry point, object wiring
│   │   ├── AppPacket.cpp       # Packet codec, CRC32, stability logic
│   │   ├── ArgParser.cpp       # --src / --dst argument parsing
│   │   └── SignalHandler.cpp   # sigaction setup, callback dispatch
│   ├── event/
│   │   └── EventLoop.cpp       # epoll_wait loop, fd registration
│   ├── socket/
│   │   └── UdpNode.cpp         # socket/bind/connect/send/recv
│   ├── thread/
│   │   └── UdpThreadManager.cpp    # pthread create, affinity, SCHED_FIFO
│   └── timer/
│       └── Timer.cpp           # timerfd_create, timerfd_settime
│
├── config/                     # Runtime configuration (reserved)
└── script/                     # Utility scripts (reserved)
```

---

## Module Details

### app - Application Layer

| Class / Function  | Responsibility                                            |
|:------------------|:----------------------------------------------------------|
| `main()`          | Wires all objects, runs event loop until shutdown         |
| `AppPacket`       | Encode/decode packets with CRC32 integrity                |
|                   | Track lifesign, measure interval, detect comm loss        |
| `ArgParser`       | Parse `--src <addr>:<port> --dst <addr>:<port>` from CLI  |
| `SignalHandler`   | Singleton; installs SIGINT/SIGTERM via `sigaction()`      |
|                   | Thread-safe shutdown flag with `std::atomic`              |
|                   | Registers callback to stop `EventLoop` on signal          |
|                   | Supports registering additional shutdown callbacks        |

### event - Event Loop

| Class        | Responsibility                                       |
|:-------------|:-----------------------------------------------------|
| `EventLoop`  | Wraps Linux `epoll` for multiplexed I/O              |
|              | Registers file descriptors with callbacks            |
|              | Drives timer expiration and (optionally) socket I/O  |

### socket - Network I/O

| Class      | Responsibility                                   |
|:-----------|:-------------------------------------------------|
| `UdpNode`  | Creates UDP datagram socket (`SOCK_DGRAM`)       |
|            | Sets `SO_REUSEADDR` for quick restart            |
|            | Binds to source address/port                     |
|            | Connects to destination (enables `send`/`recv`)  |
|            | Exposes file descriptor for epoll or thread I/O  |

### timer - Timer Management

| Class          | Responsibility                                    |
|:---------------|:--------------------------------------------------|
| `TimerHandle`  | Wraps Linux `timerfd` for high-resolution timers  |
|                | Supports one-shot and periodic modes              |
|                | Accepts `std::function<void()>` callbacks         |
|                | Exposes fd for epoll registration                 |

### thread - Threading Infrastructure

| Class                 | Responsibility                                               |
|:----------------------|:-------------------------------------------------------------|
| `UdpThreadManager`    | Creates and manages dedicated RX and TX pthreads             |
|                       | Blocks SIGINT/SIGTERM in worker threads (`pthread_sigmask`)  |
|                       | Configures CPU affinity (`pthread_setaffinity_np`)           |
|                       | Configures SCHED_FIFO real-time scheduling                   |
|                       | Tunes SO_RCVBUF / SO_SNDBUF socket buffer sizes              |
|                       | Sets SO_RCVTIMEO for clean RX thread shutdown                |
|                       | Handles ECONNREFUSED as transient (peer not ready)           |
|                       | Provides packet counters and drop statistics                 |
| `LockFreeRingBuffer`  | SPSC ring buffer (template, header-only)                     |
|                       | Cache-line aligned (`alignas(64)`) to prevent false sharing  |
|                       | Acquire/release memory ordering for thread safety            |
|                       | Default: 1024 slots x 2048 bytes per slot                    |

### stats - Latency Benchmarking

| Class / Template  | Responsibility                                                      |
|:------------------|:--------------------------------------------------------------------|
| `LatencyStats<>`  | Collects nanosecond latency samples in a lock-free circular buffer  |
|                   | Computes p50, p95, p99, p99.9, p99.99 percentiles on demand         |
|                   | O(1) recording, O(N log N) computation via snapshot + sort          |
|                   | Cache-line aligned atomics, header-only template                    |
|                   | RAII `ScopedMeasurement` for automatic timing                       |
| `TerminalUI`      | Split-screen ANSI terminal with pinned dashboard (upper 7 lines)    |
|                   | Scrolling packet log in lower region                                |
|                   | Mutex-protected output for thread safety (RX thread + main thread)  |
|                   | Dashboard shows count, min, p50, p95, p99, p99.9, max per metric    |

Three latency metrics are measured in `UdpThreadManager`:
- **TX Send Latency** — `sendto()` syscall duration
- **RX Processing Latency** — `recvfrom()` return through queue push + callback
- **RX Inter-Packet Interval** — time between consecutive received packets (jitter)

See [README_LATENCY_BENCHMARKING.md](README_LATENCY_BENCHMARKING.md) for full
architecture, percentile methodology, and performance analysis guide.

---

## Build

### Prerequisites

- Linux (kernel 2.6.27+ for `timerfd`, `epoll`)
- GCC 14+ or Clang 18+ (C++26 support)
- CMake 3.20+

### Commands

```bash
# Build
make build

# Clean
make clean

# Run (requires root for SCHED_FIFO)
make run
```

Or manually:

```bash
mkdir -p build && cd build
cmake ..
make
```

---

## Usage

```bash
# Node A
sudo ./build/agent_team_test --src 127.0.0.1:5000 --dst 127.0.0.1:6000

# Node B (separate terminal)
sudo ./build/agent_team_test --src 127.0.0.1:6000 --dst 127.0.0.1:5000
```

Or use the convenience make targets (can start in either order):

```bash
# Terminal 1
make test_node_1

# Terminal 2
make test_node_2
```

`sudo` is required for real-time scheduling (SCHED_FIFO). Alternative:

```bash
sudo setcap cap_sys_nice+ep ./build/agent_team_test
```

### Runtime Output

```
=== High-Performance UDP Configuration ===
Source:      0x7F000001:5000
Destination: 0x7F000001:6000
RX Thread:   CPU core 2, priority 80 (SCHED_FIFO)
TX Thread:   CPU core 3, priority 70 (SCHED_FIFO)
SO_RCVBUF:   2097152 bytes
SO_SNDBUF:   1048576 bytes
==========================================

[TX] Lifesign: 1, Queued: 32 bytes (TX queue: 0)
[RX] UniqueId: 0x12345678, Lifesign: 1, DataLen: 15, Interval: 100023 us
```

Press **Ctrl+C** for graceful shutdown.

---

## Coding Conventions

This project follows safety-critical coding practices:

- **MISRA-like boolean checks**: `if (condition == true)` / `if (condition == false)`
- **Always use braces**: All `if`, `else`, `while`, `for` statements use `{}`
- **Single point return**: Each function has exactly one `return` statement at the end
- **Prefer `do-while`**: Loop bodies execute at least once (except event loops)
- **No `continue`**: Loop flow is controlled via conditions, not `continue` statements
- **Explicit comparisons**: Pointers compared with `nullptr`, booleans with `true`/`false`

---

## License

MIT License - Copyright (c) 2026 Tawan Thintawornkul
