/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file SignalHandler.cpp
 * @ingroup app
 * @brief Signal handler implementation
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "app/SignalHandler.hpp"

#include <iostream>
#include <format>
#include <unistd.h>

/*******************************************************************************
 * Static Members
 ******************************************************************************/
std::atomic<bool> SignalHandler::s_shutdownRequested(false);
std::atomic<int> SignalHandler::s_signalNumber(0);

/*******************************************************************************
 * Constructor/Destructor
 ******************************************************************************/

SignalHandler::SignalHandler()
    : m_initialized(false)
{
}

SignalHandler::~SignalHandler()
{
    // Restore default signal handlers
    if (m_initialized == true)
    {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
    }
}

/*******************************************************************************
 * Public Methods
 ******************************************************************************/

SignalHandler&
SignalHandler::getInstance()
{
    static SignalHandler instance;
    return instance;
}

bool
SignalHandler::initialize()
{
    bool result = false;

    if (m_initialized == true)
    {
        std::cerr << "SignalHandler: Already initialized" << std::endl;
    }
    else
    {
        // Set up SIGINT handler (Ctrl+C)
        struct sigaction sa_int;
        sa_int.sa_handler = signalHandlerFunction;
        sigemptyset(&sa_int.sa_mask);
        sa_int.sa_flags = 0;

        if (sigaction(SIGINT, &sa_int, nullptr) < 0)
        {
            std::cerr << "SignalHandler: Failed to set SIGINT handler" << std::endl;
        }
        else
        {
            // Set up SIGTERM handler
            struct sigaction sa_term;
            sa_term.sa_handler = signalHandlerFunction;
            sigemptyset(&sa_term.sa_mask);
            sa_term.sa_flags = 0;

            if (sigaction(SIGTERM, &sa_term, nullptr) < 0)
            {
                std::cerr << "SignalHandler: Failed to set SIGTERM handler" << std::endl;
            }
            else
            {
                m_initialized = true;
                std::cout << "SignalHandler: Initialized (press Ctrl+C to stop)" << std::endl;
                result = true;
            }
        }
    }

    return result;
}

void
SignalHandler::registerCallback(ShutdownCallback callback)
{
    m_callbacks.push_back(callback);
}

bool
SignalHandler::isShutdownRequested() const
{
    return s_shutdownRequested.load(std::memory_order_acquire);
}

void
SignalHandler::waitForShutdown()
{
    do
    {
        usleep(100000);  // Sleep 100ms
    }
    while (isShutdownRequested() == false);
}

void
SignalHandler::reset()
{
    s_shutdownRequested.store(false, std::memory_order_release);
    s_signalNumber.store(0, std::memory_order_release);
}

int
SignalHandler::getSignalNumber() const
{
    return s_signalNumber.load(std::memory_order_acquire);
}

/*******************************************************************************
 * Private Methods
 ******************************************************************************/

void
SignalHandler::signalHandlerFunction(int signum)
{
    // Signal-safe operations only
    s_signalNumber.store(signum, std::memory_order_release);
    s_shutdownRequested.store(true, std::memory_order_release);

    // Get instance and execute callbacks (not strictly signal-safe, but acceptable)
    SignalHandler& handler = getInstance();
    handler.executeCallbacks(signum);

    // Write message to stdout (write() is signal-safe)
    const char* msg = nullptr;
    switch (signum)
    {
        case SIGINT:
            msg = "\n[Signal] Received SIGINT (Ctrl+C), initiating shutdown...\n";
            break;
        case SIGTERM:
            msg = "\n[Signal] Received SIGTERM, initiating shutdown...\n";
            break;
        default:
            msg = "\n[Signal] Received signal, initiating shutdown...\n";
            break;
    }

    if (msg != nullptr)
    {
        // Use write() instead of std::cout for signal safety
        size_t len = 0;
        do
        {
            len++;
        }
        while (msg[len] != '\0');
        
        write(STDOUT_FILENO, msg, len);
    }
}

void
SignalHandler::executeCallbacks(int signum)
{
    for (auto& callback : m_callbacks)
    {
        if (callback != nullptr)
        {
            callback(signum);
        }
    }
}
