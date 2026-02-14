/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file main.cpp
 * @ingroup main
 * 
 * @brief Entry point for the Agent Team Test application
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <format>
#include <iostream>
#include <cstring>
#include <sys/epoll.h>

#include "app/ArgParser.hpp"
#include "app/AppPacket.hpp"
#include "app/SignalHandler.hpp"
#include "event/EventLoop.hpp"
#include "socket/UdpNode.hpp"
#include "timer/timer.hpp"
#include "thread/UdpThreadManager.hpp"
#include "stats/TerminalUI.hpp"


/*******************************************************************************
 * Enum / Structure
 ******************************************************************************/

/*******************************************************************************
 * Constant
 ******************************************************************************/
static constexpr uint32_t TX_INTERVAL_MS         = 100U;   /**< TX timer interval (ms) */
static constexpr uint32_t COMM_MONITOR_MS        = 200U;   /**< Comm monitor interval (ms) */
static constexpr uint32_t COMM_TIMEOUT_MS        = 1000U;  /**< Comm loss threshold (ms) */
static constexpr size_t   UDP_RX_BUFFER_SIZE     = 512U;   /**< UDP receive buffer size */
static constexpr uint32_t STATS_REPORT_INTERVAL_MS = 250U;   /**< Latency stats report interval (ms) */

// Thread configuration
static constexpr int      RX_CPU_CORE            = 2;       /**< CPU core for RX thread */
static constexpr int      TX_CPU_CORE            = 3;       /**< CPU core for TX thread */
static constexpr int      RX_RT_PRIORITY         = 80;      /**< RX real-time priority (1-99) */
static constexpr int      TX_RT_PRIORITY         = 70;      /**< TX real-time priority (1-99) */
static constexpr size_t   SO_RCVBUF_SIZE         = 2097152; /**< 2MB RX socket buffer */
static constexpr size_t   SO_SNDBUF_SIZE         = 1048576; /**< 1MB TX socket buffer */


/*******************************************************************************
 * Global Object
 ******************************************************************************/

/*******************************************************************************
 * Function Prototype
 ******************************************************************************/
static void rxPacketHandler(const uint8_t* data, size_t length, AppPacket& rx_packet, TerminalUI& ui);
static void commMonitorCallback(AppPacket& rx_packet, EventLoop& loop, TerminalUI& ui);
static void txTimerCallback(UdpThreadManager& threadMgr, AppPacket& tx_packet, TerminalUI& ui);
static void statsReportCallback(UdpThreadManager& threadMgr, TerminalUI& ui);


/*******************************************************************************
 * Main
 ******************************************************************************/
int
main(int argc, char* argv[])
{
    UdpPeerArgs peer_args = {0};
    int main_ret = EXIT_FAILURE;

    // Initialize signal handler for graceful shutdown
    SignalHandler& signalHandler = SignalHandler::getInstance();
    if (signalHandler.initialize() == false)
    {
        std::cerr << "Failed to initialize signal handler" << std::endl;
        main_ret = EXIT_FAILURE;
        goto main_exit;
    }

    if (parseUdpPeerArgs(argc, argv, peer_args) == false)
    {
        std::cerr << std::format(
            "Usage: {} --src <addr>:<port> --dst <addr>:<port>\n",
            argv[0])
            << std::endl;
        main_ret = EXIT_FAILURE;
        goto main_exit;
    }

    std::cout << std::format(
        "=== High-Performance UDP Configuration ===\n"
        "Source:      0x{:08X}:{}\n"
        "Destination: 0x{:08X}:{}\n"
        "RX Thread:   CPU core {}, priority {} (SCHED_FIFO)\n"
        "TX Thread:   CPU core {}, priority {} (SCHED_FIFO)\n"
        "SO_RCVBUF:   {} bytes\n"
        "SO_SNDBUF:   {} bytes\n"
        "==========================================\n",
        peer_args.src_addr, peer_args.src_port,
        peer_args.dst_addr, peer_args.dst_port,
        RX_CPU_CORE, RX_RT_PRIORITY,
        TX_CPU_CORE, TX_RT_PRIORITY,
        SO_RCVBUF_SIZE, SO_SNDBUF_SIZE)
        << std::endl;

    {
        /* Initialize UDP Node */
        UdpNode udp_node;
        udp_node.initialize(peer_args.src_addr,
                            peer_args.src_port,
                            peer_args.dst_addr,
                            peer_args.dst_port);

        if (udp_node.getError() != UdpNode::UdpNodeError::None)
        {
            std::cerr << "Failed to initialize UDP node" << std::endl;
            main_ret = EXIT_FAILURE;
            goto main_exit;
        }

        /* Initialize Terminal UI (declared early so callbacks can reference it) */
        TerminalUI ui;

        /* Initialize TX packet */
        AppPacket tx_packet;
        tx_packet.setUniqueId(0x12345678U);

        /* Initialize RX packet */
        AppPacket rx_packet;
        rx_packet.setCommTimeout(COMM_TIMEOUT_MS);
        rx_packet.setExpectedInterval(TX_INTERVAL_MS, APP_PACKET_INTERVAL_TOLERANCE_US);

        /* Initialize UDP Thread Manager */
        UdpThreadManager threadMgr;
        UdpThreadManager::Config threadConfig = {
            .rxCpuCore = RX_CPU_CORE,
            .txCpuCore = TX_CPU_CORE,
            .rxPriority = RX_RT_PRIORITY,
            .txPriority = TX_RT_PRIORITY,
            .useRealtimeScheduling = true,
            .rxBufferSize = SO_RCVBUF_SIZE,
            .txBufferSize = SO_SNDBUF_SIZE
        };

        // Set RX callback to process received packets
        threadMgr.setRxCallback([&rx_packet, &ui](const uint8_t* data, size_t length) {
            rxPacketHandler(data, length, rx_packet, ui);
        });

        // Start RX/TX threads
        if (threadMgr.start(udp_node, threadConfig) == false)
        {
            std::cerr << "Failed to start UDP thread manager" << std::endl;
            main_ret = EXIT_FAILURE;
            goto main_exit;
        }

        /* Initialize event loop for timers */
        EventLoop loop;
        loop.initialize(0);

        /* TX timer: periodic packet transmission */
        TimerHandle tx_timer;
        tx_timer.initialize(TimerHandle::msec2nsec(TX_INTERVAL_MS), true);
        tx_timer.setCallback([&threadMgr, &tx_packet, &ui]() {
            txTimerCallback(threadMgr, tx_packet, ui);
        });

        /* Comm monitor timer: periodic communication loss check */
        TimerHandle comm_monitor_timer;
        comm_monitor_timer.initialize(TimerHandle::msec2nsec(COMM_MONITOR_MS), true);
        comm_monitor_timer.setCallback([&rx_packet, &loop, &ui]() {
            commMonitorCallback(rx_packet, loop, ui);
        });

        /* Latency stats report timer: periodic percentile stats output */
        TimerHandle stats_timer;
        stats_timer.initialize(TimerHandle::msec2nsec(STATS_REPORT_INTERVAL_MS), true);
        stats_timer.setCallback([&threadMgr, &ui]() {
            statsReportCallback(threadMgr, ui);
        });

        /* Register TX timer event */
        loop.registerEvent(tx_timer.getFd(), EPOLLIN, [&tx_timer]() {
            tx_timer.handleEvent();
        });

        /* Register Comm monitor timer event */
        loop.registerEvent(comm_monitor_timer.getFd(), EPOLLIN, [&comm_monitor_timer]() {
            comm_monitor_timer.handleEvent();
        });

        /* Register Stats report timer event */
        loop.registerEvent(stats_timer.getFd(), EPOLLIN, [&stats_timer]() {
            stats_timer.handleEvent();
        });

        // Register shutdown callback to stop event loop on signal
        signalHandler.registerCallback([&loop](int) {
            loop.stop();
        });

        /* Initialize split-screen terminal UI */
        ui.initialize();

        // Run event loop until signal received
        while (signalHandler.isShutdownRequested() == false)
        {
            loop.run();
        }

        /* Restore terminal before printing shutdown summary */
        ui.shutdown();

        std::cout << "\nShutting down..." << std::endl;
        threadMgr.stop();

        main_ret = EXIT_SUCCESS;
    }

main_exit:
    return main_ret;
}


/*******************************************************************************
 * Function Definition
 ******************************************************************************/

/**
 * @brief RX packet handler
 *
 * Called from RX thread via callback when packet is received.
 * Decodes and processes the packet.
 *
 * @param[in] data Pointer to received data
 * @param[in] length Length of received data
 * @param[in,out] rx_packet Reference to RX packet for decoding
 */
static void
rxPacketHandler(const uint8_t* data, size_t length, AppPacket& rx_packet, TerminalUI& ui)
{
    bool decode_ok = rx_packet.decode(data, length);

    if (decode_ok == true)
    {
        ui.log(std::format(
            "[RX] UniqueId: 0x{:08X}, Lifesign: {}, DataLen: {}, Interval: {} us\n",
            rx_packet.getUniqueId(),
            rx_packet.getReceivedLifesign(),
            rx_packet.getDataLength(),
            rx_packet.getLastIntervalUs()));

        if (rx_packet.isCommUnstable() == true)
        {
            ui.log(std::format(
                "[RX] Warning: Communication unstable (count: {})\n",
                rx_packet.getUnstableCounter()));
        }
    }
    else
    {
        ui.log(std::format(
            "[RX] Decode failed: error code {}\n",
            static_cast<int>(rx_packet.getError())));
    }
}


/**
 * @brief Communication monitor callback
 *
 * Periodic callback to check for communication loss.
 * Stops the event loop if communication is lost.
 *
 * @param[in] rx_packet Reference to RX packet for monitoring
 * @param[in,out] loop  Reference to event loop
 */
static void
commMonitorCallback(AppPacket& rx_packet, EventLoop& loop, TerminalUI& ui)
{
    if (rx_packet.isCommLost() == true)
    {
        ui.log(std::format(
            "[MONITOR] Communication lost! No packet for {} ms (threshold: {} ms)\n",
            rx_packet.getTimeSinceLastChange(),
            rx_packet.getCommTimeout()));

        /* Stop the event loop on comm loss - or handle as needed */
        /* loop.stop(); */
    }
}


/**
 * @brief TX timer callback
 *
 * Periodic callback to transmit packets via TX thread.
 *
 * @param[in,out] threadMgr Reference to thread manager
 * @param[in,out] tx_packet Reference to TX packet
 */
static void
txTimerCallback(UdpThreadManager& threadMgr, AppPacket& tx_packet, TerminalUI& ui)
{
    static const uint8_t tx_payload[] = "Agent Team Test";
    uint8_t tx_buffer[256] = {0};

    tx_packet.setDataPointer(tx_payload, sizeof(tx_payload) - 1U);

    size_t encoded_len = tx_packet.encode(tx_buffer, sizeof(tx_buffer));

    if (encoded_len > 0U)
    {
        // Queue packet for transmission via TX thread
        if (threadMgr.queueTxPacket(tx_buffer, encoded_len) == true)
        {
            ui.log(std::format(
                "[TX] Lifesign: {}, Queued: {} bytes (TX queue: {})\n",
                tx_packet.getLifesign(),
                encoded_len,
                threadMgr.getTxQueueSize()));
        }
        else
        {
            ui.log("[TX] Failed to queue packet (queue full)\n");
        }
    }
}

/**
 * @brief Latency statistics report callback
 *
 * Periodic callback to print percentile latency statistics.
 * Computes and displays p50/p95/p99/p99.9/p99.99 for TX send,
 * RX processing, and RX inter-packet interval.
 *
 * @param[in,out] threadMgr Reference to thread manager
 */
static void
statsReportCallback(UdpThreadManager& threadMgr, TerminalUI& ui)
{
    auto rxStats = threadMgr.getRxLatencyStats().computeStats();
    auto txStats = threadMgr.getTxLatencyStats().computeStats();
    auto intervalStats = threadMgr.getRxIntervalStats().computeStats();

    /* Update the pinned dashboard (upper area) */
    ui.updateStats(txStats, rxStats, intervalStats);
}
