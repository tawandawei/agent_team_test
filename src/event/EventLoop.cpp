/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file EventLoop.cpp
 * @ingroup event
 * @class EventLoop
 * @brief Event loop handler
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <iostream>
#include <format>
#include <functional>
// Timer Related Headers
#include <sys/epoll.h>

#include "event/EventLoop.hpp"


/*******************************************************************************
 * Constant
 ******************************************************************************/



/*******************************************************************************
 * Constructor/Destructor
 ******************************************************************************/
EventLoop::EventLoop()
{
    m_epollfd = -1;
    m_timfd   = -1;
    m_running = false;
    m_error   = EventLoopError::None;
}

EventLoop::~EventLoop()
{

}


/*******************************************************************************
 * Function Definition
 ******************************************************************************/
void
EventLoop::initialize(uint64_t interval_nsec)
{
    m_epollfd = epoll_create1(0);
    if (m_epollfd < 0)
    {
        m_error = EventLoopError::EventCreateFail;
        std::cerr << std::format(
            "EventLoop::initialize: Event creation failed, m_epollfd: {}\n",
            m_epollfd)
            << std::endl;
    }
    else
    {
        std::cout << std::format(
            "EventLoop::initialize: Event initialized successfully, m_epollfd: {}\n",
            m_epollfd)
            << std::endl;
    }
}

void
EventLoop::registerEvent(int fd,
                         uint32_t events,
                         std::function<void(void)> callback)
{
    struct epoll_event ev = {0};
    int ret = -1;

    ev.events = events;
    ev.data.fd = fd;

    ret = epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0)
    {
        m_error = EventLoopError::AddEventFail;
        std::cerr << std::format(
            "EventLoop::registerEvent: Failed to add event for fd: {}, events: {}, ret: {}\n",
            fd,
            events,
            ret)
            << std::endl;
    }
    else
    {
        m_callbacks[fd] = callback;
        std::cout << std::format(
            "EventLoop::registerEvent: Event added successfully for fd: {}, events: {}\n",
            fd,
            events)
            << std::endl;
    }
}

/**
 * @brief Run the event loop
 *
 * Blocks and dispatches events until stop() is called.
 */
void
EventLoop::run(void)
{
    static constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS] = {0};
    int nfds = 0;

    m_running = true;

    while (m_running == true)
    {
        nfds = epoll_wait(m_epollfd, events, MAX_EVENTS, -1);

        for (int idx = 0; idx < nfds; idx++)
        {
            int fd = events[idx].data.fd;
            auto it = m_callbacks.find(fd);

            if (it != m_callbacks.end())
            {
                it->second();  /* Invoke callback */
            }
        }
    }
}

/**
 * @brief Stop the event loop
 */
void
EventLoop::stop(void)
{
    m_running = false;
}
