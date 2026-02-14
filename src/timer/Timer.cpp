/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file Timer.cpp
 * @ingroup timer
 * @class TimerHandle
 * @brief Timer handle using Linux timerfd
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <iostream>
#include <format>
#include <unistd.h>
#include <sys/timerfd.h>

#include "timer/timer.hpp"


/*******************************************************************************
 * Constant
 ******************************************************************************/
static constexpr uint64_t NSEC_PER_SEC  = 1'000'000'000ULL;
static constexpr uint64_t NSEC_PER_MSEC = 1'000'000ULL;


/*******************************************************************************
 * Constructor/Destructor
 ******************************************************************************/
TimerHandle::TimerHandle()
{
    m_timerfd  = -1;
    m_callback = nullptr;
    m_error    = TimerHandleError::None;
}

TimerHandle::~TimerHandle()
{
    close();
}


/*******************************************************************************
 * Function Definition
 ******************************************************************************/

/**
 * @brief Initialize the timer with a specified interval
 *
 * @param[in] interval_nsec  Timer interval in nanoseconds
 * @param[in] periodic       true for periodic timer, false for one-shot
 */
void
TimerHandle::initialize(uint64_t interval_nsec, bool periodic)
{
    struct itimerspec timespec = {0};
    int ret = -1;
    uint64_t sec  = 0ULL;
    uint64_t nsec = 0ULL;

    m_timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (m_timerfd < 0)
    {
        m_error = TimerHandleError::TimerCreateFail;
        std::cerr << std::format(
            "TimerHandle::initialize: Timer creation failed, m_timerfd: {}\n",
            m_timerfd)
            << std::endl;
        goto TimerHandle_initialize_exit;
    }

    sec  = interval_nsec / NSEC_PER_SEC;
    nsec = interval_nsec % NSEC_PER_SEC;

    /* Initial expiration */
    timespec.it_value.tv_sec  = static_cast<time_t>(sec);
    timespec.it_value.tv_nsec = static_cast<long>(nsec);

    /* Periodic interval (0 for one-shot) */
    if (periodic == true)
    {
        timespec.it_interval.tv_sec  = static_cast<time_t>(sec);
        timespec.it_interval.tv_nsec = static_cast<long>(nsec);
    }

    ret = timerfd_settime(m_timerfd, 0, &timespec, nullptr);
    if (ret < 0)
    {
        m_error = TimerHandleError::SetTimeFail;
        std::cerr << std::format(
            "TimerHandle::initialize: Timer settime failed, ret: {}\n",
            ret)
            << std::endl;
        goto TimerHandle_initialize_exit;
    }

    m_error = TimerHandleError::None;
    std::cout << std::format(
        "TimerHandle::initialize: Timer initialized successfully, "
        "m_timerfd: {}, interval: {} ns, periodic: {}\n",
        m_timerfd, interval_nsec, periodic)
        << std::endl;

TimerHandle_initialize_exit:
    // Done
}

/**
 * @brief Set the callback function to be invoked on timer expiration
 *
 * @param[in] callback  Function to call when timer expires
 */
void
TimerHandle::setCallback(CallbackType callback)
{
    m_callback = callback;
}

/**
 * @brief Handle timer event - read the timerfd and invoke callback
 *
 * Call this from EventLoop when the timer fd becomes readable.
 */
void
TimerHandle::handleEvent(void)
{
    uint64_t expirations = 0ULL;
    ssize_t bytes_read = 0;

    bytes_read = read(m_timerfd, &expirations, sizeof(expirations));
    if (bytes_read != static_cast<ssize_t>(sizeof(expirations)))
    {
        m_error = TimerHandleError::ReadFail;
        std::cerr << std::format(
            "TimerHandle::handleEvent: Read failed, bytes_read: {}\n",
            bytes_read)
            << std::endl;
    }
    else
    {
        if (m_callback != nullptr)
        {
            m_callback();
        }
    }
}

/**
 * @brief Close the timer file descriptor
 */
void
TimerHandle::close(void)
{
    if (m_timerfd >= 0)
    {
        ::close(m_timerfd);
        m_timerfd = -1;
    }
}

/**
 * @brief Get the timer file descriptor for use with EventLoop
 *
 * @return Timer file descriptor
 */
int
TimerHandle::getFd(void) const
{
    return m_timerfd;
}

/**
 * @brief Get the current error state
 *
 * @return Current error code
 */
TimerHandle::TimerHandleError
TimerHandle::getError(void) const
{
    return m_error;
}

/**
 * @brief Convert milliseconds to nanoseconds
 *
 * @param[in] msec  Time in milliseconds
 * @return Time in nanoseconds
 */
uint64_t
TimerHandle::msec2nsec(uint32_t msec)
{
    return static_cast<uint64_t>(msec) * NSEC_PER_MSEC;
}

/**
 * @brief Convert seconds to nanoseconds
 *
 * @param[in] sec  Time in seconds
 * @return Time in nanoseconds
 */
uint64_t
TimerHandle::sec2nsec(uint32_t sec)
{
    return static_cast<uint64_t>(sec) * NSEC_PER_SEC;
}
