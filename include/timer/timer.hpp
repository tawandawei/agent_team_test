/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file timer.hpp
 * @ingroup timer
 * @class TimerHandle
 * @brief Timer handle using Linux timerfd
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_TIMER_TIMER_HPP
#define AGENT_TEAM_TEST_TIMER_TIMER_HPP
/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <cstdint>
#include <functional>


/*******************************************************************************
 * Macro
 ******************************************************************************/



/*******************************************************************************
 * Enum / Structure
 ******************************************************************************/



/*******************************************************************************
 * Class Declaration
 ******************************************************************************/
class TimerHandle
{
/***********************************************************
 * Enum
 **********************************************************/
public:
    enum class TimerHandleError
    {
        None,
        TimerCreateFail,
        SetTimeFail,
        ReadFail
    };

/***********************************************************
 * Type
 **********************************************************/
public:
    using CallbackType = std::function<void(void)>;

/***********************************************************
 * Constructor/Destructor
 **********************************************************/
public:
    TimerHandle();
    ~TimerHandle();

/***********************************************************
 * Method
 **********************************************************/
public:
    void initialize(uint64_t interval_nsec, bool periodic);
    void setCallback(CallbackType callback);
    void handleEvent(void);
    void close(void);

    int getFd(void) const;
    TimerHandle::TimerHandleError getError(void) const;

    static uint64_t msec2nsec(uint32_t msec);
    static uint64_t sec2nsec(uint32_t sec);

/***********************************************************
 * Helper Method
 **********************************************************/
private:

/***********************************************************
 * Data
 **********************************************************/
private:
    int m_timerfd;
    CallbackType m_callback;
    TimerHandle::TimerHandleError m_error;
};


#endif  // AGENT_TEAM_TEST_TIMER_TIMER_HPP
