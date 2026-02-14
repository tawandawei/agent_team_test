/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file EventLoop.hpp
 * @ingroup event
 * @class EventLoop
 * @brief Event loop handler
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_EVENT_EVENTLOOP_HPP
#define AGENT_TEAM_TEST_EVENT_EVENTLOOP_HPP
/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <cstdint>
#include <string_view>
#include <functional>
#include <map>


/*******************************************************************************
 * Macro
 ******************************************************************************/



/*******************************************************************************
 * Enum / Structure
 ******************************************************************************/



/*******************************************************************************
 * Class Declaration
 ******************************************************************************/
class EventLoop
{
/***********************************************************
 * Enum
 **********************************************************/
public:
    enum class EventLoopError
    {
        None,
        EventCreateFail,
        AddEventFail,
        RemoveEventFail
    };

/***********************************************************
 * Constructor/Destructor
 **********************************************************/
public:
    EventLoop();
    ~EventLoop();

/***********************************************************
 * Method
 **********************************************************/
public:
    void initialize(uint64_t interval_nsec);
    void registerEvent(int fd,
                       uint32_t events,
                       std::function<void(void)> callback);
    void run(void);
    void stop(void);

    EventLoop::EventLoopError getError(void) const;

private:
    /* Specific property */
    int m_epollfd;
    int m_timfd;
    bool m_running;
    std::map<int, std::function<void(void)>> m_callbacks;
    EventLoop::EventLoopError m_error;
};


#endif  // AGENT_TEAM_TEST_EVENT_EVENTLOOP_HPP
