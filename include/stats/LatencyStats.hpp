/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file LatencyStats.hpp
 * @ingroup stats
 * @class LatencyStats
 * @brief Lock-free latency percentile statistics collector
 *
 * Collects latency samples and computes p50, p95, p99, p99.9, p99.99
 * percentiles, plus min/max/mean/stdev. Designed for real-time systems
 * with minimal overhead using a fixed-size circular buffer.
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_STATS_LATENCYSTATS_HPP
#define AGENT_TEAM_TEST_STATS_LATENCYSTATS_HPP

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <string>
#include <format>
#include <sstream>

/*******************************************************************************
 * Macro
 ******************************************************************************/

/** Default capacity: 100,000 samples (~10 sec at 10kHz, ~2.7 hrs at 10Hz) */
static constexpr size_t LATENCY_STATS_DEFAULT_CAPACITY = 100000U;

/*******************************************************************************
 * Class Declaration
 ******************************************************************************/

/**
 * @brief High-performance latency percentile statistics collector
 *
 * Uses a fixed-size circular buffer to store raw latency samples in
 * nanoseconds. When the buffer is full, oldest samples are overwritten.
 * Percentile computation uses a snapshot + sort approach (called on-demand,
 * not on every sample).
 *
 * Thread safety:
 *   - recordSample() is safe to call from a single producer thread
 *   - computeStats() should be called from a separate consumer thread
 *     (it takes a snapshot, so it won't block the producer)
 *
 * @tparam Capacity  Maximum number of samples to retain
 */
template<size_t Capacity = LATENCY_STATS_DEFAULT_CAPACITY>
class LatencyStats
{
public:
    /**
     * @brief Computed statistics result
     */
    struct Result
    {
        uint64_t count;        /**< Total samples recorded */
        double   min_us;       /**< Minimum latency (us) */
        double   max_us;       /**< Maximum latency (us) */
        double   mean_us;      /**< Mean latency (us) */
        double   stdev_us;     /**< Standard deviation (us) */
        double   p50_us;       /**< 50th percentile / median (us) */
        double   p95_us;       /**< 95th percentile (us) */
        double   p99_us;       /**< 99th percentile (us) */
        double   p999_us;      /**< 99.9th percentile (us) */
        double   p9999_us;     /**< 99.99th percentile (us) */

        /**
         * @brief Format statistics as a human-readable string
         */
        std::string toString(const std::string& label = "Latency") const
        {
            if (count == 0U)
            {
                return std::format("[{}] No samples collected\n", label);
            }

            return std::format(
                "┌──────────────────────────────────────────────┐\n"
                "│ {:<44} │\n"
                "├──────────────────────────────────────────────┤\n"
                "│ Samples : {:<34} │\n"
                "│ Min     : {:>10.2f} us {} │\n"
                "│ Max     : {:>10.2f} us {} │\n"
                "│ Mean    : {:>10.2f} us {} │\n"
                "│ StdDev  : {:>10.2f} us                      │\n"
                "├──────────────────────────────────────────────┤\n"
                "│ p50     : {:>10.2f} us {} │\n"
                "│ p95     : {:>10.2f} us {} │\n"
                "│ p99     : {:>10.2f} us {} │\n"
                "│ p99.9   : {:>10.2f} us {} │\n"
                "│ p99.99  : {:>10.2f} us {} │\n"
                "└──────────────────────────────────────────────┘\n",
                label + " Statistics",
                count,
                min_us,  formatBar(min_us, max_us, min_us),
                max_us,  formatBar(max_us, max_us, min_us),
                mean_us, formatBar(mean_us, max_us, min_us),
                stdev_us,
                p50_us,   formatBar(p50_us, max_us, min_us),
                p95_us,   formatBar(p95_us, max_us, min_us),
                p99_us,   formatBar(p99_us, max_us, min_us),
                p999_us,  formatBar(p999_us, max_us, min_us),
                p9999_us, formatBar(p9999_us, max_us, min_us));
        }

        /**
         * @brief Format as CSV header + data line
         */
        std::string toCsv(const std::string& label = "Latency") const
        {
            return std::format(
                "{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f}\n",
                label, count,
                min_us, max_us, mean_us, stdev_us,
                p50_us, p95_us, p99_us, p999_us, p9999_us);
        }

        static std::string csvHeader()
        {
            return "label,count,min_us,max_us,mean_us,stdev_us,p50_us,p95_us,p99_us,p999_us,p9999_us\n";
        }

    private:
        static std::string formatBar(double value, double max_val, double min_val)
        {
            constexpr int BAR_WIDTH = 20;

            if (max_val <= min_val)
            {
                std::string result;
                for (int i = 0; i < BAR_WIDTH; i++) { result += "\xe2\x96\x88"; }
                return result;
            }

            double ratio = (value - min_val) / (max_val - min_val);
            int filled = static_cast<int>(ratio * BAR_WIDTH);
            filled = std::clamp(filled, 0, BAR_WIDTH);

            std::string result;
            for (int i = 0; i < filled; i++)          { result += "\xe2\x96\x88"; }  /* █ */
            for (int i = 0; i < BAR_WIDTH - filled; i++) { result += "\xe2\x96\x91"; }  /* ░ */
            return result;
        }
    };

    /**
     * @brief RAII scope timer for automatic latency measurement
     *
     * Usage:
     *   {
     *       auto scope = stats.startMeasurement();
     *       // ... code to measure ...
     *   }  // latency recorded automatically on destruction
     */
    class ScopedMeasurement
    {
    public:
        explicit ScopedMeasurement(LatencyStats& stats)
            : m_stats(stats)
            , m_start(std::chrono::steady_clock::now())
        {
        }

        ~ScopedMeasurement()
        {
            auto end = std::chrono::steady_clock::now();
            uint64_t ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - m_start).count());
            m_stats.recordSample(ns);
        }

        /* Non-copyable, non-movable */
        ScopedMeasurement(const ScopedMeasurement&) = delete;
        ScopedMeasurement& operator=(const ScopedMeasurement&) = delete;

    private:
        LatencyStats& m_stats;
        std::chrono::steady_clock::time_point m_start;
    };

public:
    /*****************************************************
     * Constructor
     ****************************************************/
    LatencyStats()
        : m_writeIdx(0U)
        , m_count(0U)
    {
        m_samples.fill(0U);
    }

    /*****************************************************
     * Recording
     ****************************************************/

    /**
     * @brief Record a latency sample in nanoseconds
     *
     * O(1) operation. Lock-free for single producer.
     *
     * @param[in] latency_ns  Latency value in nanoseconds
     */
    void recordSample(uint64_t latency_ns)
    {
        size_t idx = m_writeIdx.load(std::memory_order_relaxed);
        m_samples[idx] = latency_ns;

        m_writeIdx.store((idx + 1U) % Capacity, std::memory_order_release);
        m_count.fetch_add(1U, std::memory_order_relaxed);
    }

    /**
     * @brief Record latency from two time_points
     */
    void recordSample(std::chrono::steady_clock::time_point start,
                      std::chrono::steady_clock::time_point end)
    {
        uint64_t ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count());
        recordSample(ns);
    }

    /**
     * @brief Start a scoped measurement (RAII)
     *
     * @return ScopedMeasurement that records on destruction
     */
    ScopedMeasurement startMeasurement()
    {
        return ScopedMeasurement(*this);
    }

    /**
     * @brief Get current timestamp for manual start/stop measurement
     */
    static std::chrono::steady_clock::time_point now()
    {
        return std::chrono::steady_clock::now();
    }

    /*****************************************************
     * Computation
     ****************************************************/

    /**
     * @brief Compute percentile statistics from collected samples
     *
     * Takes a snapshot of the circular buffer, sorts it, and
     * computes percentiles. O(N log N) where N = min(count, Capacity).
     *
     * @return Result struct with all computed statistics
     */
    Result computeStats() const
    {
        Result result = {};
        uint64_t totalCount = m_count.load(std::memory_order_acquire);

        if (totalCount == 0U)
        {
            return result;
        }

        /* Determine how many valid samples we have */
        size_t numSamples = (totalCount < Capacity) ? 
                            static_cast<size_t>(totalCount) : Capacity;

        /* Take snapshot and sort */
        std::vector<uint64_t> sorted(numSamples);

        if (totalCount <= Capacity)
        {
            /* Buffer hasn't wrapped yet */
            std::copy(m_samples.begin(),
                      m_samples.begin() + static_cast<long>(numSamples),
                      sorted.begin());
        }
        else
        {
            /* Buffer has wrapped - copy from current write position */
            size_t writePos = m_writeIdx.load(std::memory_order_acquire);
            size_t firstPart = Capacity - writePos;

            std::copy(m_samples.begin() + static_cast<long>(writePos),
                      m_samples.end(),
                      sorted.begin());
            std::copy(m_samples.begin(),
                      m_samples.begin() + static_cast<long>(writePos),
                      sorted.begin() + static_cast<long>(firstPart));
        }

        std::sort(sorted.begin(), sorted.end());

        /* Basic statistics */
        result.count = totalCount;
        result.min_us = static_cast<double>(sorted.front()) / 1000.0;
        result.max_us = static_cast<double>(sorted.back()) / 1000.0;

        /* Mean and standard deviation */
        double sum = 0.0;
        double sumSq = 0.0;

        for (size_t i = 0U; i < numSamples; i++)
        {
            double val = static_cast<double>(sorted[i]) / 1000.0;  /* ns -> us */
            sum += val;
            sumSq += val * val;
        }

        double n = static_cast<double>(numSamples);
        result.mean_us = sum / n;

        if (numSamples > 1U)
        {
            double variance = (sumSq - (sum * sum / n)) / (n - 1.0);
            result.stdev_us = std::sqrt(std::max(0.0, variance));
        }

        /* Percentiles using nearest-rank method */
        result.p50_us   = percentile(sorted, 50.0);
        result.p95_us   = percentile(sorted, 95.0);
        result.p99_us   = percentile(sorted, 99.0);
        result.p999_us  = percentile(sorted, 99.9);
        result.p9999_us = percentile(sorted, 99.99);

        return result;
    }

    /*****************************************************
     * Accessors
     ****************************************************/

    /**
     * @brief Get total number of samples recorded (including overwritten)
     */
    uint64_t getSampleCount() const
    {
        return m_count.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reset all collected samples
     */
    void reset()
    {
        m_writeIdx.store(0U, std::memory_order_release);
        m_count.store(0U, std::memory_order_release);
        m_samples.fill(0U);
    }

private:
    /*****************************************************
     * Helper
     ****************************************************/

    /**
     * @brief Compute percentile from sorted data using nearest-rank method
     */
    static double percentile(const std::vector<uint64_t>& sorted, double p)
    {
        if (sorted.empty())
        {
            return 0.0;
        }

        /* Nearest-rank: ceil(p/100 * N) - 1 */
        double rank = (p / 100.0) * static_cast<double>(sorted.size());
        size_t idx = static_cast<size_t>(std::ceil(rank));

        if (idx == 0U)
        {
            idx = 1U;
        }

        if (idx > sorted.size())
        {
            idx = sorted.size();
        }

        return static_cast<double>(sorted[idx - 1U]) / 1000.0;  /* ns -> us */
    }

    /*****************************************************
     * Data
     ****************************************************/
    std::array<uint64_t, Capacity> m_samples;     /**< Circular sample buffer (ns) */
    alignas(64) std::atomic<size_t> m_writeIdx;   /**< Write index (producer) */
    alignas(64) std::atomic<uint64_t> m_count;    /**< Total samples recorded */
};


#endif  // AGENT_TEAM_TEST_STATS_LATENCYSTATS_HPP
