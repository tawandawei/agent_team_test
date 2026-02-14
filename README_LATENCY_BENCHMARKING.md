# Latency Benchmarking

## Overview

The latency benchmarking subsystem provides **real-time percentile statistics**
(p50, p95, p99, p99.9, p99.99) for UDP send/receive operations. It measures
three distinct latency dimensions across the data path and presents them in a
**split-screen terminal dashboard** that updates live without disrupting the
packet log.

Key properties:

- **O(1) sample recording** on the hot path (lock-free, no heap allocation)
- **Nanosecond precision** using `std::chrono::steady_clock`
- **Cache-line aligned** atomics to prevent false sharing between threads
- **Header-only template** for inlining the recording path into RX/TX loops
- **ANSI split-screen UI** with pinned dashboard and scrolling log

---

## Architecture

### Measurement Points

Three measurement points are instrumented inside `UdpThreadManager`:

```
  ┌─────────────────────────────────────────────────────────────────────────────┐
  │                              UdpThreadManager                               │
  │                                                                             │
  │  ┌───────────────────────────────────────────────────────────────────────┐  │
  │  │  RX Thread (CPU 2, SCHED_FIFO Prio 80)                                │  │
  │  │                                                                       │  │
  │  │                  ◄──── RX Interval ────►                              │  │
  │  │                  :                      :                             │  │
  │  │   recvfrom() ────┤                      :                             │  │
  │  │                  │  rxStart ◄───────────────────────────────┐         │  │
  │  │                  │    │                 :                   │         │  │
  │  │                  │    │  rxPacketCount++                    │         │  │
  │  │                  │    │  push(RX Ring Buffer)               │         │  │
  │  │                  │    │  invoke RX callback                 │         │  │
  │  │                  │    │                                     │         │  │
  │  │                  │  rxEnd ◄─────────────────────────────────┘         │  │
  │  │                  │    │                                               │  │
  │  │                  │    └──► m_rxLatencyStats.recordSample()            │  │
  │  │                  │            (rxEnd - rxStart)                       │  │
  │  │                  :                                                    │  │
  │  │   recvfrom() ────┤  ◄── next rxStart                                  │  │
  │  │                  │    │                                               │  │
  │  │                  │    └──► m_rxIntervalStats.recordSample()           │  │
  │  │                  │            (rxStart[N] - rxStart[N-1])             │  │
  │  │                  :                                                    │  │
  │  └───────────────────────────────────────────────────────────────────────┘  │
  │                                                                             │
  │  ┌───────────────────────────────────────────────────────────────────────┐  │
  │  │  TX Thread (CPU 3, SCHED_FIFO Prio 70)                                │  │
  │  │                                                                       │  │
  │  │   pop(TX Ring Buffer)                                                 │  │
  │  │       │                                                               │  │
  │  │     txStart ◄──────────────────────────────────────────┐              │  │
  │  │       │                                                │              │  │
  │  │       │  sendto(socket)                                │              │  │
  │  │       │                                                │              │  │
  │  │     txEnd ◄────────────────────────────────────────────┘              │  │
  │  │       │                                                               │  │
  │  │       └──► m_txLatencyStats.recordSample()                            │  │
  │  │               (txEnd - txStart)                                       │  │
  │  │                                                                       │  │
  │  └───────────────────────────────────────────────────────────────────────┘  │
  │                                                                             │
  └─────────────────────────────────────────────────────────────────────────────┘
```

| Metric                        | What It Measures                                   | Where                               |
|:------------------------------|:---------------------------------------------------|:------------------------------------|
| **RX Processing Latency**     | `recvfrom()` return → queue push + callback done   | `UdpThreadManager::rxThreadLoop()`  |
| **TX Send Latency**           | `sendto()` syscall duration                        | `UdpThreadManager::txThreadLoop()`  |
| **RX Inter-Packet Interval**  | Time between consecutive `recvfrom()` completions  | `UdpThreadManager::rxThreadLoop()`  |

### Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            stats module                                 │
│                                                                         │
│  ┌─────────────────────────────┐     ┌──────────────────────────────┐   │
│  │     LatencyStats<>          │     │     TerminalUI               │   │
│  │     (header-only template)  │     │     (header-only)            │   │
│  │                             │     │                              │   │
│  │  ┌───────────────────────┐  │     │  ┌────────────────────────┐  │   │
│  │  │ Circular Buffer       │  │     │  │ Upper: Dashboard       │  │   │
│  │  │ 100,000 x uint64_t    │  │     │  │ (7 lines, pinned)      │  │   │
│  │  │ (nanosecond samples)  │  │     │  ├────────────────────────┤  │   │
│  │  └───────────────────────┘  │     │  │ Lower: Packet Log      │  │   │
│  │                             │     │  │ (scroll region)        │  │   │
│  │  recordSample() ── O(1)     │     │  └────────────────────────┘  │   │
│  │  computeStats() ── O(N lg N)│     │                              │   │
│  │                             │     │  initialize() / shutdown()   │   │
│  │  Result:                    │     │  updateStats()               │   │
│  │    p50, p95, p99,           │     │  log()                       │   │
│  │    p99.9, p99.99            │     │                              │   │
│  │    min, max, mean, stdev    │     │  ANSI escape sequences:      │   │
│  └─────────────────────────────┘     │    \033[H    cursor home     │   │
│                                      │    \033[s/u  save/restore    │   │
│                                      │    \033[r    scroll region   │   │
│                                      └──────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Data Flow

```
RX Thread                          Main Thread (Event Loop)
─────────                          ────────────────────────

recvfrom()                         TX Timer (100ms)
   │                                  │
   │ clock::now() ──► rxStart         │ encodes packet
   │                                  │ queueTxPacket()
   │ push + callback                  │
   │                                  │
   │ clock::now() ──► rxEnd           Stats Timer (10s)
   │                                  │
   ▼                                  │ computeStats() ←── snapshot + sort
m_rxLatencyStats.recordSample()       │
m_rxIntervalStats.recordSample()      │ ui.updateStats() ←── redraw dashboard
                                      │
TX Thread                             │ ui.log() ←── packet messages scroll
─────────                             │

pop(TX Ring Buffer)
   │
   │ clock::now() ──► txStart
   │
   │ sendto()
   │
   │ clock::now() ──► txEnd
   ▼
m_txLatencyStats.recordSample()
```

---

## Module Details

### LatencyStats (include/stats/LatencyStats.hpp)

Header-only template class. Kept header-only because:
1. It is a template (`template<size_t Capacity>`)
2. `recordSample()` must be inlineable on the hot path (every packet)
3. Follows the same pattern as `LockFreeRingBuffer.hpp`

#### Storage

```cpp
std::array<uint64_t, Capacity> m_samples;   // Circular buffer (nanoseconds)
alignas(64) std::atomic<size_t> m_writeIdx;  // Producer write index
alignas(64) std::atomic<uint64_t> m_count;   // Total samples (including overwritten)
```

- Default capacity: **100,000 samples**
- At 10 Hz TX rate: retains ~2.7 hours of data
- At 10 kHz: retains ~10 seconds
- Cache-line alignment prevents false sharing between producer and consumer

#### Recording (Hot Path)

```cpp
void recordSample(uint64_t latency_ns);                          // Direct ns
void recordSample(time_point start, time_point end);             // Two timestamps
ScopedMeasurement startMeasurement();                            // RAII scope
```

All recording methods are **O(1)** with no allocations. Memory ordering:
- `m_writeIdx`: relaxed load, release store (publish sample)
- `m_count`: relaxed fetch_add (statistics only)

#### Computation (Cold Path)

```cpp
Result computeStats() const;
```

Called periodically (every 10 seconds by default). Steps:
1. Snapshot the circular buffer into a `std::vector`
2. Sort the snapshot — **O(N log N)**
3. Compute percentiles using the **nearest-rank method**
4. Compute mean and standard deviation in a single pass

#### Result Struct

| Field       | Type      | Description                        |
|:------------|:----------|:-----------------------------------|
| `count`     | uint64_t  | Total samples recorded             |
| `min_us`    | double    | Minimum latency (microseconds)     |
| `max_us`    | double    | Maximum latency (microseconds)     |
| `mean_us`   | double    | Arithmetic mean (microseconds)     |
| `stdev_us`  | double    | Standard deviation (microseconds)  |
| `p50_us`    | double    | 50th percentile / median           |
| `p95_us`    | double    | 95th percentile                    |
| `p99_us`    | double    | 99th percentile                    |
| `p999_us`   | double    | 99.9th percentile                  |
| `p9999_us`  | double    | 99.99th percentile                 |

Output formats: `toString()` (table with bar chart), `toCsv()` (CSV line).

#### Percentile Method

Uses **nearest-rank** (ceiling):

$$P_k = x_{\lceil \frac{k}{100} \cdot N \rceil}$$

Where $k$ is the percentile (e.g., 99), $N$ is the sample count, and $x$ is the
sorted sample array. This is the same method used by HDR Histogram, wrk, and
other standard benchmarking tools.

### TerminalUI (include/stats/TerminalUI.hpp)

Header-only class that manages a split-screen terminal using ANSI escape codes.

#### Screen Layout

```
Line 1:  ┌──────────────────────────────────────────────────────────┐
         │ UDP Latency Dashboard                    (reverse video) │
Line 2:  │           count       min       p50       p95     ...    │
Line 3:  │ ---------------------------------------------------------│
Line 4:  │ TX Send    253       3.2       8.0      36.5     ...     │  PINNED
Line 5:  │ RX Proc    145      10.4      21.8      34.3     ...     │  (fixed)
Line 6:  │ RX Intv    144   99613.2   99997.7  100082.8     ...     │
Line 7:  │ -------------------- Packet Log  ------------------------│
         └──────────────────────────────────────────────────────────┘
Line 8+: [TX] Lifesign: 254, Queued: 27 bytes (TX queue: 0)       ← scrolls
         [RX] UniqueId: 0x12345678, Lifesign: 253, ...            ← scrolls
         [TX] Lifesign: 255, Queued: 27 bytes (TX queue: 0)       ← scrolls
         ...                                                      ← scrolls
```

#### ANSI Escape Sequences Used

| Sequence             | Purpose                           |
|:---------------------|:----------------------------------|
| `\033[2J`            | Clear entire screen               |
| `\033[H`             | Move cursor to home (1,1)         |
| `\033[s` / `\033[u`  | Save / restore cursor position    |
| `\033[r`             | Reset scroll region               |
| `\033[8;Nr`          | Set scroll region lines 8 to N    |
| `\033[1;7m`          | Bold + reverse video (title bar)  |
| `\033[2m`            | Dim text (headers/separators)     |
| `\033[0m`            | Reset all attributes              |
| `\033[K`             | Clear to end of line              |

#### Thread Safety

All output goes through a single `std::mutex`:
- `updateStats()` — called from main thread (stats timer callback)
- `log()` — called from RX thread and main thread (TX timer callback)
- `shutdown()` — called from main thread before exit

#### Lifecycle

```
main() {
    TerminalUI ui;
    // ... setup UDP, threads, timers ...

    ui.initialize();       // clear screen, draw dashboard, set scroll region

    while (running) {
        loop.run();        // stats timer → ui.updateStats()
                           // RX callback → ui.log()
                           // TX callback → ui.log()
    }

    ui.shutdown();         // reset scroll region, restore terminal
    threadMgr.stop();      // prints full stats tables to normal stdout
}
```

---

## Integration with UdpThreadManager

Three `LatencyStats<>` instances are members of `UdpThreadManager`:

```cpp
class UdpThreadManager {
    // ...
    LatencyStats<> m_rxLatencyStats;      // RX processing latency
    LatencyStats<> m_txLatencyStats;      // TX send latency
    LatencyStats<> m_rxIntervalStats;     // RX inter-packet interval
    std::chrono::steady_clock::time_point m_lastRxTime;
    bool m_firstRxPacket;
};
```

Accessor methods:

```cpp
LatencyStats<>& getRxLatencyStats();
LatencyStats<>& getTxLatencyStats();
LatencyStats<>& getRxIntervalStats();
```

On `threadMgr.stop()`, final statistics with full percentile tables (including
bar charts) are printed to stdout for post-session analysis.

---

## Configuration

| Parameter                         | Default   | Location            | Description                          |
|:----------------------------------|:----------|:--------------------|:-------------------------------------|
| `STATS_REPORT_INTERVAL_MS`        | 250 msec  | `main.cpp`          | Dashboard refresh interval           |
| `LATENCY_STATS_DEFAULT_CAPACITY`  | 100,000   | `LatencyStats.hpp`  | Circular buffer sample count         |
| `HEADER_LINES`                    | 7         | `TerminalUI.hpp`    | Lines reserved for pinned dashboard  |

---

## Interpreting Results

### What the Percentiles Mean

| Percentile  | Interpretation                                                       |
|:------------|:---------------------------------------------------------------------|
| **p50**     | Typical (median) latency — half of all packets are faster            |
| **p95**     | 1 in 20 packets is slower than this                                  |
| **p99**     | 1 in 100 — the "tail" that matters for real-time systems             |
| **p99.9**   | 1 in 1,000 — rare spikes (scheduling, interrupts, page faults)       |
| **p99.99**  | 1 in 10,000 — worst-case outliers (GC-like pauses, lock contention)  |

### Example Output (Shutdown Summary)

```
┌──────────────────────────────────────────────┐
│ TX Send Latency Statistics                   │
├──────────────────────────────────────────────┤
│ Samples : 253                                │
│ Min     :       3.16 us █░░░░░░░░░░░░░░░░░░░ │
│ Max     :      45.72 us ████████████████████ │
│ Mean    :      11.91 us █████░░░░░░░░░░░░░░░ │
│ StdDev  :       9.78 us                      │
├──────────────────────────────────────────────┤
│ p50     :       8.04 us ██░░░░░░░░░░░░░░░░░░ │
│ p95     :      36.48 us ████████████████░░░░ │
│ p99     :      43.62 us ███████████████████░ │
│ p99.9   :      45.72 us ████████████████████ │
│ p99.99  :      45.72 us ████████████████████ │
└──────────────────────────────────────────────┘
```

### What to Look For

| Observation                 | Likely Cause                                  | Fix                                     |
|:----------------------------|:----------------------------------------------|:----------------------------------------|
| p99 >> p50 (large tail)     | Scheduling jitter, context switches           | CPU isolation, `isolcpus=`, SCHED_FIFO  |
| p99.9 spikes periodically   | Kernel timer ticks, RCU callbacks             | `nohz_full=`, `rcu_nocbs=` on RT cores  |
| RX Interval stdev > 100 us  | TX jitter on remote peer, network congestion  | Check remote peer stats, network path   |
| TX Send p99 > 100 us        | SO_SNDBUF full, NIC TX ring full              | Increase SO_SNDBUF, check NIC settings  |
| Growing drop count          | Ring buffer full, consumer too slow           | Increase ring buffer capacity           |

---

## Performance Overhead

The benchmarking subsystem is designed to add negligible overhead:

| Operation         | Cost                       | Frequency     |
|:------------------|:---------------------------|:--------------|
| `recordSample()`  | ~15-30 ns                  | Every packet  |
| `clock::now()`    | ~20-25 ns (x2 per packet)  | Every packet  |
| `computeStats()`  | ~1-5 ms (100K sort)        | Every 10 sec  |
| `updateStats()`   | ~50-100 us (ANSI I/O)      | Every 10 sec  |
| Memory            | ~800 KB per LatencyStats   | 3 instances   |

Total hot-path overhead per packet: **~60-80 ns** (two `clock::now()` calls +
one `recordSample()`), which is <0.1% of a 100 ms packet interval.

---

## File Summary

| File                              | Type                  | Role                                  |
|:----------------------------------|:----------------------|:--------------------------------------|
| `include/stats/LatencyStats.hpp`  | Header-only template  | Percentile statistics collector       |
| `include/stats/TerminalUI.hpp`    | Header-only class     | Split-screen ANSI terminal dashboard  |

---

## Future Enhancements

1. **HDR Histogram** — Replace sorted-array with compressed histogram for O(1) percentiles
2. **CSV Export** — Dump per-sample data to file for offline analysis with gnuplot/matplotlib
3. **End-to-End Latency** — Embed TX timestamp in packet payload, measure sender → receiver
4. **Histogram Visualization** — ASCII histogram bucket distribution in terminal
5. **Configurable Percentiles** — User-defined percentile list via config file
6. **Window Reset** — Auto-reset statistics window for rolling percentiles
