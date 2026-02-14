/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file UdpThreadManager.cpp
 * @ingroup thread
 * @brief UDP thread manager implementation
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "thread/UdpThreadManager.hpp"

#include <sched.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <format>

/*******************************************************************************
 * Constructor/Destructor
 ******************************************************************************/

UdpThreadManager::UdpThreadManager()
    : m_rxThread(0)
    , m_txThread(0)
    , m_running(false)
    , m_udpNode(nullptr)
    , m_config{}
    , m_rxCallback(nullptr)
    , m_error(Error::None)
    , m_rxPacketCount(0)
    , m_txPacketCount(0)
    , m_rxDropCount(0)
    , m_txDropCount(0)
    , m_lastRxTime(std::chrono::steady_clock::now())
    , m_firstRxPacket(true)
{
}

UdpThreadManager::~UdpThreadManager()
{
    stop();
}

/*******************************************************************************
 * Public Methods
 ******************************************************************************/

bool
UdpThreadManager::start(UdpNode& udpNode, const Config& config)
{
    bool result = false;

    if (m_running.load(std::memory_order_acquire) == true)
    {
        std::cerr << "UdpThreadManager: Already running" << std::endl;
    }
    else
    {
        m_udpNode = &udpNode;
        m_config = config;
        m_error = Error::None;
        
        // Configure socket buffers
        if (configureSocketBuffers() == false)
        {
            m_error = Error::SetSocketBufferFail;
        }
        else
        {
            m_running.store(true, std::memory_order_release);
            
            // Create RX thread
            if (pthread_create(&m_rxThread, nullptr, rxThreadEntry, this) != 0)
            {
                std::cerr << "UdpThreadManager: Failed to create RX thread: " 
                          << strerror(errno) << std::endl;
                m_error = Error::ThreadCreateFail;
                m_running.store(false, std::memory_order_release);
            }
            else
            {
                // Create TX thread
                if (pthread_create(&m_txThread, nullptr, txThreadEntry, this) != 0)
                {
                    std::cerr << "UdpThreadManager: Failed to create TX thread: " 
                              << strerror(errno) << std::endl;
                    m_error = Error::ThreadCreateFail;
                    m_running.store(false, std::memory_order_release);
                    pthread_join(m_rxThread, nullptr);
                }
                else
                {
                    // Configure RX thread
                    if (configureThread(m_rxThread, config.rxCpuCore, config.rxPriority, config.useRealtimeScheduling) == false)
                    {
                        std::cerr << "UdpThreadManager: Failed to configure RX thread" << std::endl;
                        // Continue anyway - not fatal
                    }
                    
                    // Configure TX thread
                    if (configureThread(m_txThread, config.txCpuCore, config.txPriority, config.useRealtimeScheduling) == false)
                    {
                        std::cerr << "UdpThreadManager: Failed to configure TX thread" << std::endl;
                        // Continue anyway - not fatal
                    }
                    
                    std::cout << std::format(
                        "UdpThreadManager: Started\n"
                        "  RX: CPU core {}, priority {} {}\n"
                        "  TX: CPU core {}, priority {} {}\n"
                        "  RX buffer: {} bytes, TX buffer: {} bytes\n",
                        config.rxCpuCore, config.rxPriority, config.useRealtimeScheduling ? "(SCHED_FIFO)" : "",
                        config.txCpuCore, config.txPriority, config.useRealtimeScheduling ? "(SCHED_FIFO)" : "",
                        config.rxBufferSize, config.txBufferSize)
                        << std::endl;
                    
                    result = true;
                }
            }
        }
    }
    
    return result;
}

void
UdpThreadManager::stop()
{
    if (m_running.load(std::memory_order_acquire) == false)
    {
        return;
    }
    
    m_running.store(false, std::memory_order_release);
    
    // Wait for threads to finish
    if (m_rxThread != 0)
    {
        pthread_join(m_rxThread, nullptr);
        m_rxThread = 0;
    }
    
    if (m_txThread != 0)
    {
        pthread_join(m_txThread, nullptr);
        m_txThread = 0;
    }
    
    std::cout << std::format(
        "UdpThreadManager: Stopped\n"
        "  RX packets: {}, dropped: {}\n"
        "  TX packets: {}, dropped: {}\n",
        m_rxPacketCount.load(), m_rxDropCount.load(),
        m_txPacketCount.load(), m_txDropCount.load())
        << std::endl;

    /* Print latency statistics on shutdown */
    auto rxStats = m_rxLatencyStats.computeStats();
    auto txStats = m_txLatencyStats.computeStats();
    auto intervalStats = m_rxIntervalStats.computeStats();

    std::cout << rxStats.toString("RX Processing Latency");
    std::cout << txStats.toString("TX Send Latency");
    std::cout << intervalStats.toString("RX Inter-Packet Interval");
}

void
UdpThreadManager::setRxCallback(RxCallback callback)
{
    m_rxCallback = callback;
}

bool
UdpThreadManager::queueTxPacket(const uint8_t* data, size_t length)
{
    bool result = true;

    if (m_txQueue.push(data, length) == false)
    {
        m_txDropCount.fetch_add(1, std::memory_order_relaxed);
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Private Methods
 ******************************************************************************/

void*
UdpThreadManager::rxThreadEntry(void* arg)
{
    UdpThreadManager* manager = static_cast<UdpThreadManager*>(arg);
    manager->rxThreadLoop();
    return nullptr;
}

void*
UdpThreadManager::txThreadEntry(void* arg)
{
    UdpThreadManager* manager = static_cast<UdpThreadManager*>(arg);
    manager->txThreadLoop();
    return nullptr;
}

void
UdpThreadManager::rxThreadLoop()
{
    uint8_t rxBuffer[2048];
    bool shouldExit = false;

    // Block SIGINT/SIGTERM so signals are delivered to the main thread
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigmask, nullptr);

    std::cout << "RX thread started (TID: " << gettid() << ")" << std::endl;
    
    do
    {
        // Blocking receive from socket
        ssize_t recvLen = m_udpNode->receive(rxBuffer, sizeof(rxBuffer));
        
        if (recvLen > 0)
        {
            auto rxStart = std::chrono::steady_clock::now();

            m_rxPacketCount.fetch_add(1, std::memory_order_relaxed);

            /* Measure inter-packet interval (jitter) */
            if (m_firstRxPacket == false)
            {
                m_rxIntervalStats.recordSample(m_lastRxTime, rxStart);
            }
            else
            {
                m_firstRxPacket = false;
            }
            m_lastRxTime = rxStart;
            
            // Push to queue for application processing
            if (m_rxQueue.push(rxBuffer, static_cast<size_t>(recvLen)) == false)
            {
                m_rxDropCount.fetch_add(1, std::memory_order_relaxed);
            }
            
            // If callback is set, call it directly (bypass queue)
            if (m_rxCallback != nullptr)
            {
                m_rxCallback(rxBuffer, static_cast<size_t>(recvLen));
            }

            /* Record RX processing latency: recvfrom completion -> callback done */
            auto rxEnd = std::chrono::steady_clock::now();
            m_rxLatencyStats.recordSample(rxStart, rxEnd);
        }
        else if (recvLen < 0)
        {
            // Error - check if it's a transient error
            // ECONNREFUSED occurs on connected UDP when peer is not ready (ICMP unreachable)
            bool isTransientError = ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) || (errno == ECONNREFUSED));
            
            if (isTransientError == false)
            {
                std::cerr << "RX thread: receive error: " << strerror(errno) << std::endl;
                shouldExit = true;
            }
        }
    }
    while ((m_running.load(std::memory_order_acquire) == true) && (shouldExit == false));
    
    std::cout << "RX thread stopped" << std::endl;
}

void
UdpThreadManager::txThreadLoop()
{
    uint8_t txBuffer[2048];
    size_t txLength;

    // Block SIGINT/SIGTERM so signals are delivered to the main thread
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigmask, nullptr);

    std::cout << "TX thread started (TID: " << gettid() << ")" << std::endl;
    
    do
    {
        // Try to get packet from queue
        if (m_txQueue.pop(txBuffer, sizeof(txBuffer), txLength) == true)
        {
            auto txStart = std::chrono::steady_clock::now();

            // Send packet
            ssize_t sentLen = m_udpNode->send(txBuffer, txLength);

            auto txEnd = std::chrono::steady_clock::now();
            
            if (sentLen > 0)
            {
                m_txPacketCount.fetch_add(1, std::memory_order_relaxed);
                /* Record TX send latency: sendto() call duration */
                m_txLatencyStats.recordSample(txStart, txEnd);
            }
            else
            {
                m_txDropCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else
        {
            // Queue empty - yield CPU briefly
            usleep(10);  // 10 microseconds
        }
    }
    while (m_running.load(std::memory_order_acquire) == true);
    
    std::cout << "TX thread stopped" << std::endl;
}

bool
UdpThreadManager::configureThread(pthread_t thread, int cpuCore, int priority, bool useRealtime)
{
    bool result = true;
    
    // Set CPU affinity
    if (cpuCore >= 0)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpuCore, &cpuset);
        
        if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0)
        {
            std::cerr << std::format(
                "Failed to set CPU affinity to core {}: {}\n",
                cpuCore, strerror(errno))
                << std::endl;
            result = false;
        }
        else
        {
            std::cout << std::format("Set CPU affinity to core {}\n", cpuCore) << std::endl;
        }
    }
    
    // Set real-time scheduling (SCHED_FIFO)
    if ((useRealtime == true) && (priority > 0))
    {
        struct sched_param param;
        param.sched_priority = priority;
        
        if (pthread_setschedparam(thread, SCHED_FIFO, &param) != 0)
        {
            std::cerr << std::format(
                "Failed to set SCHED_FIFO priority {}: {}\n"
                "Note: May require root privileges or CAP_SYS_NICE capability\n",
                priority, strerror(errno))
                << std::endl;
            result = false;
        }
        else
        {
            std::cout << std::format("Set SCHED_FIFO priority {}\n", priority) << std::endl;
        }
    }
    
    return result;
}

bool
UdpThreadManager::configureSocketBuffers()
{
    bool result = false;
    int sockFd = m_udpNode->getFd();
    
    if (sockFd < 0)
    {
        std::cerr << "Invalid socket file descriptor" << std::endl;
    }
    else
    {
        result = true;
        
        // Set SO_RCVBUF
        if (m_config.rxBufferSize > 0)
        {
            int rxBufSize = static_cast<int>(m_config.rxBufferSize);
            if (setsockopt(sockFd, SOL_SOCKET, SO_RCVBUF, &rxBufSize, sizeof(rxBufSize)) < 0)
            {
                std::cerr << std::format(
                    "Failed to set SO_RCVBUF to {} bytes: {}\n",
                    m_config.rxBufferSize, strerror(errno))
                    << std::endl;
                result = false;
            }
            else
            {
                // Verify actual size set
                int actualSize = 0;
                socklen_t optlen = sizeof(actualSize);
                getsockopt(sockFd, SOL_SOCKET, SO_RCVBUF, &actualSize, &optlen);
                std::cout << std::format("SO_RCVBUF set to {} bytes (requested {})\n",
                                         actualSize, m_config.rxBufferSize) << std::endl;
            }
        }
        
        // Set SO_SNDBUF
        if ((result == true) && (m_config.txBufferSize > 0))
        {
            int txBufSize = static_cast<int>(m_config.txBufferSize);
            if (setsockopt(sockFd, SOL_SOCKET, SO_SNDBUF, &txBufSize, sizeof(txBufSize)) < 0)
            {
                std::cerr << std::format(
                    "Failed to set SO_SNDBUF to {} bytes: {}\n",
                    m_config.txBufferSize, strerror(errno))
                    << std::endl;
                result = false;
            }
            else
            {
                // Verify actual size set
                int actualSize = 0;
                socklen_t optlen = sizeof(actualSize);
                getsockopt(sockFd, SOL_SOCKET, SO_SNDBUF, &actualSize, &optlen);
                std::cout << std::format("SO_SNDBUF set to {} bytes (requested {})\n",
                                         actualSize, m_config.txBufferSize) << std::endl;
            }
        }

        // Set SO_RCVTIMEO so RX thread can periodically check running flag
        if (result == true)
        {
            struct timeval recv_timeout;
            recv_timeout.tv_sec = 0;
            recv_timeout.tv_usec = 100000;  // 100ms
            if (setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0)
            {
                std::cerr << std::format(
                    "Failed to set SO_RCVTIMEO: {}\n",
                    strerror(errno))
                    << std::endl;
                // Non-fatal: RX thread will still work, just may not exit cleanly
            }
            else
            {
                std::cout << "SO_RCVTIMEO set to 100 ms" << std::endl;
            }
        }
    }
    
    return result;
}
