# High-Performance UDP Threading Implementation

## Overview

This implementation provides ultra-low latency and high throughput UDP communication using:
- **Separate RX/TX threads** with lock-free ring buffers
- **Real-time scheduling** (SCHED_FIFO) with configurable priorities
- **CPU affinity** pinning threads to specific cores
- **Signal masking** in worker threads (SIGINT/SIGTERM delivered to main thread only)
- **SO_RCVBUF/SO_SNDBUF tuning** for increased socket buffer sizes
- **SO_REUSEADDR** for quick restart after shutdown
- **SO_RCVTIMEO** for clean RX thread shutdown
- **ECONNREFUSED tolerance** so nodes can start in any order
- **Cache-line aligned** data structures to prevent false sharing

## Architecture

```
┌──────────────────┐           ┌──────────────────┐
│   RX Thread      │──push───▶│  RX Ring Buffer  │
│  (CPU Core 2)    │           │   2048 x 1024    │
│  Priority: 80    │           └──────────────────┘
│  SIGINT blocked  │             │
└──────────────────┘             │  (also direct RX callback
       ▲                         │   invoked from RX thread)
       │                         ▼
  recvfrom()               ┌─────────────────┐
  (SO_RCVTIMEO:            │  Application    │
   100 ms timeout)         │  (Main Thread)  │
       │                   │  Event Loop     │
┌─────────────────┐        └─────────────────┘
│  UDP Socket     │            │
│  SO_REUSEADDR   │            │
│  SO_RCVBUF: 2MB │            ▼
│  SO_SNDBUF: 1MB │   ┌──────────────────┐
└─────────────────┘   │  TX Ring Buffer  │
       ▲              │   2048 x 1024    │
       │              └──────────────────┘
  sendto()                    │
       │              ┌──────────────────┐
       └──────pop─────│   TX Thread      │
                      │  (CPU Core 3)    │
                      │  Priority: 70    │
                      │  SIGINT blocked  │
                      └──────────────────┘
```

## Key Features

### 1. Lock-Free Ring Buffers
- **SPSC (Single Producer Single Consumer)** design
- Cache-line aligned (64 bytes) to prevent false sharing
- Memory ordering: `memory_order_acquire/release`
- Capacity: 1024 packets per queue
- Max packet size: 2048 bytes

### 2. RX Thread (High Priority)
- **CPU Core**: 2 (configurable via `RX_CPU_CORE`)
- **Priority**: 80 (configurable via `RX_RT_PRIORITY`)
- **Scheduling**: SCHED_FIFO real-time
- **Signal Mask**: SIGINT/SIGTERM blocked (`pthread_sigmask`)
- **Behavior**: Blocking receive with `SO_RCVTIMEO` (100 ms) for clean shutdown check
- **Callback**: Direct callback to application for zero-copy
- **Resilience**: `ECONNREFUSED` treated as transient (peer not yet listening)

### 3. TX Thread (Medium Priority)
- **CPU Core**: 3 (configurable via `TX_CPU_CORE`)
- **Priority**: 70 (configurable via `TX_RT_PRIORITY`)
- **Scheduling**: SCHED_FIFO real-time
- **Signal Mask**: SIGINT/SIGTERM blocked (`pthread_sigmask`)
- **Behavior**: Pops from ring buffer, blocking send

### 4. Socket Configuration
- **SO_REUSEADDR**: Enables quick restart without `TIME_WAIT` delay
- **SO_RCVBUF**: 2MB (2,097,152 bytes) - prevents kernel packet drops
- **SO_SNDBUF**: 1MB (1,048,576 bytes) - transmission buffering
- **SO_RCVTIMEO**: 100 ms - allows RX thread to periodically check `m_running` flag

## Configuration

Edit `src/app/main.cpp` to customize:

```cpp
// Thread configuration
static constexpr int      RX_CPU_CORE            = 2;        // CPU core for RX
static constexpr int      TX_CPU_CORE            = 3;        // CPU core for TX
static constexpr int      RX_RT_PRIORITY         = 80;       // 1-99 (higher = more priority)
static constexpr int      TX_RT_PRIORITY         = 70;       // 1-99
static constexpr size_t   SO_RCVBUF_SIZE         = 2097152;  // 2MB
static constexpr size_t   SO_SNDBUF_SIZE         = 1048576;  // 1MB
```

## Building

```bash
make build
```

## Running

**Important**: Real-time scheduling requires elevated privileges:

```bash
# Using make targets (recommended, can start in either order)
make test_node_1    # Terminal 1: src 127.0.0.1:5000 -> dst 127.0.0.1:6000
make test_node_2    # Terminal 2: src 127.0.0.1:6000 -> dst 127.0.0.1:5000

# Or run manually with sudo
sudo ./build/agent_team_test --src 127.0.0.1:5000 --dst 127.0.0.1:6000

# Or grant CAP_SYS_NICE capability
sudo setcap cap_sys_nice+ep ./build/agent_team_test
./build/agent_team_test --src 127.0.0.1:5000 --dst 127.0.0.1:6000
```

### Testing with Two Instances

Nodes can be started in either order. The RX thread tolerates `ECONNREFUSED`
(peer not yet listening) and keeps retrying until the peer comes up.

Terminal 1 (Node A):
```bash
make test_node_1
```

Terminal 2 (Node B):
```bash
make test_node_2
```

Or manually:

Terminal 1 (Node A):
```bash
sudo ./build/agent_team_test --src 127.0.0.1:5000 --dst 127.0.0.1:6000
```

Terminal 2 (Node B):
```bash
sudo ./build/agent_team_test --src 127.0.0.1:6000 --dst 127.0.0.1:5000
```

## Performance Considerations

### Latency Optimization
1. **CPU Isolation**: Isolate CPUs 2 and 3 from kernel scheduler
   ```bash
   # Add to kernel boot parameters:
   isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
   ```

2. **IRQ Affinity**: Move network IRQs to other cores
   ```bash
   # Find network IRQ
   cat /proc/interrupts | grep eth0
   # Set affinity to core 0
   echo 1 > /proc/irq/<IRQ_NUM>/smp_affinity
   ```

3. **Disable CPU Frequency Scaling**
   ```bash
   sudo cpupower frequency-set -g performance
   ```

4. **Huge Pages** (for even lower latency)
   ```bash
   echo 128 > /proc/sys/vm/nr_hugepages
   ```

### Throughput Optimization
- Increase ring buffer size in `LockFreeRingBuffer` template
- Batch processing: modify TX thread to send multiple packets per iteration
- Use `SO_BUSY_POLL` socket option for busy polling

### Monitoring

Runtime statistics are printed on shutdown:
```
UdpThreadManager: Stopped
  RX packets: 1000, dropped: 0
  TX packets: 1000, dropped: 0
```

During execution:
- `[RX]` - Received packet with interval timing
- `[TX]` - Queued packet with current TX queue size
- `[MONITOR]` - Communication loss detection

## Real-Time Scheduling Notes

### Permission Requirements
- **Root**: Full access to SCHED_FIFO
- **CAP_SYS_NICE**: Grant specific binary capability
- **ulimit**: Set real-time priority limits in `/etc/security/limits.conf`

```
<username> soft rtprio 99
<username> hard rtprio 99
```

### Priority Guidelines
- **90-99**: Critical system tasks (avoid)
- **70-89**: High-priority application threads (RX thread)
- **50-69**: Medium-priority threads (TX thread)
- **1-49**: Low-priority real-time tasks

## Implementation Details

### Memory Layout
```cpp
// Cache-line aligned to prevent false sharing
alignas(64) std::atomic<size_t> m_writeIdx;  // Producer cache line
alignas(64) std::atomic<size_t> m_readIdx;   // Consumer cache line
```

### Memory Ordering
- **Relaxed**: Same-thread operations (writeIdx load before storing)
- **Acquire**: Read synchronization (check if data available)
- **Release**: Write synchronization (publish new data)

### Thread Safety
- RX thread: Single writer to RX queue, invokes RX callback directly
- TX thread: Single reader from TX queue
- Main thread (event loop): Single writer to TX queue (via timer callback)
- Signal handling: SIGINT/SIGTERM blocked in worker threads, delivered to main thread
- Shutdown: `SignalHandler` callback stops `EventLoop`; `m_running` flag stops worker threads
- No locks required - lockless synchronization via atomics

## Troubleshooting

### "Failed to set SCHED_FIFO priority"
- Run with `sudo` or grant `CAP_SYS_NICE` capability
- Check `/etc/security/limits.conf` for rtprio limits

### "SO_RCVBUF set to X bytes (requested Y)"
- Kernel limits buffer size
- Increase: `sudo sysctl -w net.core.rmem_max=4194304`
- Make permanent: Add to `/etc/sysctl.conf`

### High Packet Loss
- Increase ring buffer capacity
- Check CPU isolation
- Verify RX thread priority
- Monitor CPU usage: `top -H -p <PID>`

### Poor Latency
- Verify CPU affinity: `taskset -cp <PID>`
- Check scheduler policy: `chrt -p <PID>`
- Disable CPU frequency scaling
- Move IRQs away from designated cores

## Benchmarking

Test latency with high-resolution timestamps:
```cpp
// Add to rxPacketHandler():
auto now = std::chrono::high_resolution_clock::now();
auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
    now - tx_timestamp);
std::cout << "Latency: " << latency.count() << " us\n";
```

Expected performance:
- **Latency**: <50 μs (microseconds) on dedicated cores
- **Throughput**: >100k packets/sec (small packets)
- **Jitter**: <10 μs with proper OS tuning

## Future Enhancements

1. **Memory Pool**: Pre-allocated packet buffers (eliminates malloc)
2. **Zero-Copy Buffer**: Eliminate memcpy in ring buffer
3. **DPDK Integration**: Kernel bypass for <1 μs latency
4. **Batch Processing**: Process multiple packets per loop iteration
5. **Busy Polling**: SO_BUSY_POLL for sub-microsecond latency
