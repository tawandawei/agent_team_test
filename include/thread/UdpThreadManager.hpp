/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file UdpThreadManager.hpp
 * @ingroup thread
 * @brief UDP thread manager with RX/TX separation and real-time scheduling
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_THREAD_UDPTHREADMANAGER_HPP
#define AGENT_TEAM_TEST_THREAD_UDPTHREADMANAGER_HPP

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <pthread.h>
#include <atomic>
#include <functional>
#include <cstdint>

#include "thread/LockFreeRingBuffer.hpp"
#include "socket/UdpNode.hpp"
#include "stats/LatencyStats.hpp"

/*******************************************************************************
 * Class Declaration
 ******************************************************************************/

class UdpThreadManager
{
public:
    using RxCallback = std::function<void(const uint8_t*, size_t)>;
    
    struct Config
    {
        int rxCpuCore;          /**< CPU core for RX thread (-1 = no affinity) */
        int txCpuCore;          /**< CPU core for TX thread (-1 = no affinity) */
        int rxPriority;         /**< Real-time priority for RX thread (1-99) */
        int txPriority;         /**< Real-time priority for TX thread (1-99) */
        bool useRealtimeScheduling;  /**< Enable SCHED_FIFO */
        size_t rxBufferSize;    /**< SO_RCVBUF size in bytes */
        size_t txBufferSize;    /**< SO_SNDBUF size in bytes */
    };
    
    enum class Error
    {
        None,
        ThreadCreateFail,
        SetAffinityFail,
        SetSchedulerFail,
        SetSocketBufferFail
    };

public:
    UdpThreadManager();
    ~UdpThreadManager();
    
    /**
     * @brief Initialize and start RX/TX threads
     * 
     * @param udpNode Reference to UDP node
     * @param config Thread configuration
     * @return true if successful
     */
    bool start(UdpNode& udpNode, const Config& config);
    
    /**
     * @brief Stop RX/TX threads
     */
    void stop();
    
    /**
     * @brief Set RX callback for received packets
     */
    void setRxCallback(RxCallback callback);
    
    /**
     * @brief Queue packet for transmission
     * 
     * @param data Pointer to packet data
     * @param length Length of packet
     * @return true if successfully queued
     */
    bool queueTxPacket(const uint8_t* data, size_t length);
    
    /**
     * @brief Get RX queue statistics
     */
    size_t getRxQueueSize() const { return m_rxQueue.size(); }
    
    /**
     * @brief Get TX queue statistics
     */
    size_t getTxQueueSize() const { return m_txQueue.size(); }
    
    /**
     * @brief Get last error
     */
    Error getError() const { return m_error; }
    
    /**
     * @brief Get RX packet counter
     */
    uint64_t getRxPacketCount() const { return m_rxPacketCount.load(std::memory_order_relaxed); }
    
    /**
     * @brief Get TX packet counter
     */
    uint64_t getTxPacketCount() const { return m_txPacketCount.load(std::memory_order_relaxed); }

    /**
     * @brief Get RX latency statistics (recvfrom → callback completion)
     */
    LatencyStats<>& getRxLatencyStats() { return m_rxLatencyStats; }

    /**
     * @brief Get TX latency statistics (queue pop → sendto completion)
     */
    LatencyStats<>& getTxLatencyStats() { return m_txLatencyStats; }

    /**
     * @brief Get RX interval jitter statistics (time between consecutive packets)
     */
    LatencyStats<>& getRxIntervalStats() { return m_rxIntervalStats; }

private:
    /**
     * @brief RX thread entry point
     */
    static void* rxThreadEntry(void* arg);
    
    /**
     * @brief TX thread entry point
     */
    static void* txThreadEntry(void* arg);
    
    /**
     * @brief RX thread main loop
     */
    void rxThreadLoop();
    
    /**
     * @brief TX thread main loop
     */
    void txThreadLoop();
    
    /**
     * @brief Configure thread with CPU affinity and real-time scheduling
     */
    bool configureThread(pthread_t thread, int cpuCore, int priority, bool useRealtime);
    
    /**
     * @brief Configure socket buffer sizes
     */
    bool configureSocketBuffers();

private:
    pthread_t m_rxThread;
    pthread_t m_txThread;
    std::atomic<bool> m_running;
    
    UdpNode* m_udpNode;
    Config m_config;
    
    LockFreeRingBuffer<2048, 1024> m_rxQueue;  // RX: socket -> application
    LockFreeRingBuffer<2048, 1024> m_txQueue;  // TX: application -> socket
    
    RxCallback m_rxCallback;
    Error m_error;
    
    std::atomic<uint64_t> m_rxPacketCount;
    std::atomic<uint64_t> m_txPacketCount;
    std::atomic<uint64_t> m_rxDropCount;
    std::atomic<uint64_t> m_txDropCount;

    /* Latency statistics */
    LatencyStats<> m_rxLatencyStats;     /**< RX processing latency */
    LatencyStats<> m_txLatencyStats;     /**< TX send latency */
    LatencyStats<> m_rxIntervalStats;    /**< RX inter-packet interval jitter */
    std::chrono::steady_clock::time_point m_lastRxTime;  /**< For interval measurement */
    bool m_firstRxPacket;                /**< Skip interval on first packet */
};

#endif  // AGENT_TEAM_TEST_THREAD_UDPTHREADMANAGER_HPP
