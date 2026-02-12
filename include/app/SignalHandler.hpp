/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file SignalHandler.hpp
 * @ingroup app
 * @class SignalHandler
 * @brief Signal handler for graceful application shutdown
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_APP_SIGNALHANDLER_HPP
#define AGENT_TEAM_TEST_APP_SIGNALHANDLER_HPP

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <atomic>
#include <functional>
#include <vector>
#include <signal.h>

/*******************************************************************************
 * Class Declaration
 ******************************************************************************/

/**
 * @brief Signal handler for graceful application shutdown
 * 
 * Handles SIGINT (Ctrl+C) and SIGTERM signals to allow graceful shutdown.
 * Thread-safe and allows registering callbacks to be executed on signal receipt.
 */
class SignalHandler
{
public:
    using ShutdownCallback = std::function<void(int)>;

    /**
     * @brief Get singleton instance
     * 
     * @return Reference to SignalHandler instance
     */
    static SignalHandler& getInstance();

    /**
     * @brief Initialize signal handlers
     * 
     * Sets up handlers for SIGINT and SIGTERM.
     * 
     * @return true if successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Register a callback to be executed on signal
     * 
     * @param callback Function to call when signal is received
     */
    void registerCallback(ShutdownCallback callback);

    /**
     * @brief Check if shutdown was requested
     * 
     * @return true if signal was received
     */
    bool isShutdownRequested() const;

    /**
     * @brief Wait for shutdown signal
     * 
     * Blocks until a shutdown signal is received.
     */
    void waitForShutdown();

    /**
     * @brief Reset shutdown flag (for testing)
     */
    void reset();

    /**
     * @brief Get the signal number that triggered shutdown
     * 
     * @return Signal number (0 if no signal received)
     */
    int getSignalNumber() const;

private:
    SignalHandler();
    ~SignalHandler();

    // Delete copy constructor and assignment operator
    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

    /**
     * @brief Static signal handler function
     * 
     * Called by the OS when signal is received.
     * 
     * @param signum Signal number
     */
    static void signalHandlerFunction(int signum);

    /**
     * @brief Execute all registered callbacks
     * 
     * @param signum Signal number
     */
    void executeCallbacks(int signum);

private:
    static std::atomic<bool> s_shutdownRequested;
    static std::atomic<int> s_signalNumber;
    
    std::vector<ShutdownCallback> m_callbacks;
    bool m_initialized;
};

#endif  // AGENT_TEAM_TEST_APP_SIGNALHANDLER_HPP
