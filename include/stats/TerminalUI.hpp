/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file TerminalUI.hpp
 * @ingroup stats
 * @class TerminalUI
 * @brief Split-screen terminal UI with pinned latency dashboard
 *
 * Uses ANSI escape sequences to create a split-screen terminal:
 *   - Upper area (fixed): Compact latency statistics dashboard
 *   - Lower area (scrolling): Packet log messages
 *
 * The scroll region is set so that log messages only scroll within
 * the lower portion, keeping the dashboard always visible.
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_STATS_TERMINALUI_HPP
#define AGENT_TEAM_TEST_STATS_TERMINALUI_HPP

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <sys/ioctl.h>
#include <unistd.h>
#include <mutex>
#include <string>
#include <format>
#include <iostream>
#include <cstdint>

#include "stats/LatencyStats.hpp"


/*******************************************************************************
 * Class Declaration
 ******************************************************************************/
class TerminalUI
{
/***********************************************************
 * Constant
 **********************************************************/
public:
    /** Number of lines reserved for the pinned header area */
    static constexpr int HEADER_LINES = 7;

/***********************************************************
 * Constructor/Destructor
 **********************************************************/
public:
    TerminalUI()
        : m_rows(24)
        , m_cols(80)
        , m_initialized(false)
    {
    }

    ~TerminalUI()
    {
        shutdown();
    }

    /* Non-copyable */
    TerminalUI(const TerminalUI&) = delete;
    TerminalUI& operator=(const TerminalUI&) = delete;

/***********************************************************
 * Method
 **********************************************************/
public:
    /**
     * @brief Initialize the split-screen terminal UI
     *
     * Queries terminal size, clears screen, draws initial dashboard,
     * and sets the scroll region to the lower portion.
     */
    void initialize()
    {
        /* Query terminal dimensions */
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
        {
            m_rows = ws.ws_row;
            m_cols = ws.ws_col;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        /* Clear entire screen and move cursor to home */
        std::cout << "\033[2J\033[H";

        /* Draw initial empty dashboard */
        LatencyStats<>::Result empty{};
        drawDashboard(empty, empty, empty);

        /* Set scroll region: lines [HEADER_LINES+1, m_rows] */
        std::cout << "\033[" << (HEADER_LINES + 1) << ";" << m_rows << "r";

        /* Move cursor to first line of scroll region */
        std::cout << "\033[" << (HEADER_LINES + 1) << ";1H";

        std::cout << std::flush;
        m_initialized = true;
    }

    /**
     * @brief Update the pinned dashboard with new statistics
     *
     * Saves cursor position, redraws the dashboard in the fixed
     * upper area, then restores cursor to the scroll region.
     *
     * @param[in] tx        TX send latency statistics
     * @param[in] rx        RX processing latency statistics
     * @param[in] interval  RX inter-packet interval statistics
     */
    void updateStats(const LatencyStats<>::Result& tx,
                     const LatencyStats<>::Result& rx,
                     const LatencyStats<>::Result& interval)
    {
        if (m_initialized == false) { return; }

        std::lock_guard<std::mutex> lock(m_mutex);

        /* Save cursor position in scroll region */
        std::cout << "\033[s";

        /* Redraw dashboard */
        drawDashboard(tx, rx, interval);

        /* Restore cursor to previous position in scroll region */
        std::cout << "\033[u" << std::flush;
    }

    /**
     * @brief Write a log message to the scrolling lower area
     *
     * Thread-safe. Before initialization, falls back to direct stdout.
     *
     * @param[in] msg  Message string (should end with \\n)
     */
    void log(const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << msg << std::flush;
    }

    /**
     * @brief Restore terminal to normal state
     *
     * Resets the scroll region and moves cursor to the bottom.
     * Safe to call multiple times.
     */
    void shutdown()
    {
        if (m_initialized == false) { return; }

        std::lock_guard<std::mutex> lock(m_mutex);

        /* Reset scroll region to full terminal */
        std::cout << "\033[r";

        /* Move cursor to bottom of screen */
        std::cout << "\033[" << m_rows << ";1H\n";

        std::cout << std::flush;
        m_initialized = false;
    }

    /**
     * @brief Check if the UI is initialized
     */
    bool isInitialized() const { return m_initialized; }

/***********************************************************
 * Helper Method
 **********************************************************/
private:
    /**
     * @brief Draw the complete dashboard in the upper fixed area
     *
     * Layout (7 lines):
     *   Line 1: Title bar (reverse video)
     *   Line 2: Column headers
     *   Line 3: Separator
     *   Line 4: TX Send data row
     *   Line 5: RX Processing data row
     *   Line 6: RX Interval data row
     *   Line 7: Separator with "Packet Log" label
     */
    void drawDashboard(const LatencyStats<>::Result& tx,
                       const LatencyStats<>::Result& rx,
                       const LatencyStats<>::Result& interval)
    {
        /* Move cursor to top-left */
        std::cout << "\033[H";

        /* Line 1: Title bar */
        std::string title = " UDP Latency Dashboard";
        int pad = m_cols - static_cast<int>(title.size());
        if (pad < 0) { pad = 0; }

        std::cout << "\033[1;7m"   /* Bold + Reverse video */
                  << title << std::string(static_cast<size_t>(pad), ' ')
                  << "\033[0m\n";

        /* Line 2: Column headers */
        std::cout << "\033[2m"     /* Dim */
                  << std::format(" {:<8}{:>6} {:>9} {:>9} {:>9} {:>9} {:>9} {:>9}  (us)",
                                 "", "count", "min", "p50", "p95", "p99", "p99.9", "max")
                  << "\033[0m\033[K\n";

        /* Line 3: Separator */
        int sepLen = m_cols - 2;
        if (sepLen > 78) { sepLen = 78; }
        if (sepLen < 10) { sepLen = 10; }

        std::cout << "\033[2m "    /* Dim */
                  << std::string(static_cast<size_t>(sepLen), '-')
                  << "\033[0m\033[K\n";

        /* Lines 4-6: Data rows */
        drawDataRow("TX Send", tx);
        drawDataRow("RX Proc", rx);
        drawDataRow("RX Intv", interval);

        /* Line 7: Separator with Packet Log label */
        int leftDash = 20;
        int rightDash = m_cols - leftDash - 14 - 2;  /* 14 = " Packet Log  " */
        if (rightDash < 4)  { rightDash = 4; }
        if (rightDash > 50) { rightDash = 50; }

        std::cout << "\033[2m "    /* Dim */
                  << std::string(static_cast<size_t>(leftDash), '-')
                  << " Packet Log  "
                  << std::string(static_cast<size_t>(rightDash), '-')
                  << "\033[0m\033[K";
        /* No newline after last header line to avoid scrolling */
    }

    /**
     * @brief Draw a single data row in the dashboard
     *
     * @param[in] label  Row label (max 8 chars)
     * @param[in] r      Statistics result
     */
    void drawDataRow(const char* label, const LatencyStats<>::Result& r)
    {
        if (r.count == 0U)
        {
            std::cout << std::format(" {:<8}{:>6} {:>9} {:>9} {:>9} {:>9} {:>9} {:>9}",
                                     label, "-", "-", "-", "-", "-", "-", "-")
                      << "\033[K\n";
        }
        else
        {
            std::cout << std::format(" {:<8}{:>6} {:>9.1f} {:>9.1f} {:>9.1f} {:>9.1f} {:>9.1f} {:>9.1f}",
                                     label, r.count,
                                     r.min_us, r.p50_us, r.p95_us,
                                     r.p99_us, r.p999_us, r.max_us)
                      << "\033[K\n";
        }
    }

/***********************************************************
 * Data
 **********************************************************/
private:
    int m_rows;             /**< Terminal height (rows) */
    int m_cols;             /**< Terminal width (columns) */
    bool m_initialized;     /**< Whether the TUI is active */
    std::mutex m_mutex;     /**< Protects all terminal output */
};


#endif  /* AGENT_TEAM_TEST_STATS_TERMINALUI_HPP */
