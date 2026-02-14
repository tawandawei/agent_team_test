/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file UdpNode.cpp
 * @ingroup socket
 * @class UdpNode
 * @brief UDP socket handler
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
// Network Related Headers
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <format>

#include "socket/UdpNode.hpp"



/*******************************************************************************
 * Constant
 ******************************************************************************/



/*******************************************************************************
 * Constructor/Destructor
 ******************************************************************************/
UdpNode::UdpNode()
{
    m_sockfd = -1;
    m_error = UdpNodeError::None;
}

UdpNode::~UdpNode()
{
    close();
}


/*******************************************************************************
 * Function Definition
 ******************************************************************************/
void
UdpNode::initialize(uint32_t src_addr, uint16_t src_port,
                    uint32_t dst_addr, uint16_t dst_port)
{
    int ret = -1;
    struct sockaddr_in recv_addr = {0};
    struct sockaddr_in snd_addr = {0};

    m_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sockfd < 0)
    {
        m_error = UdpNodeError::SocketCreateFail;
        std::cerr << std::format(
            "UdpNode::initialize: Socket creation failed\n"
            "m_sockfd: {}\n",
            m_sockfd)
            << std::endl;
        goto UdpNode_initialize_exit;
    }

    {
        int reuse = 1;
        if (setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            std::cerr << "UdpNode::initialize: Failed to set SO_REUSEADDR" << std::endl;
        }
    }

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = htonl(src_addr);
    recv_addr.sin_port = htons(src_port);

    snd_addr.sin_family = AF_INET;
    snd_addr.sin_addr.s_addr = htonl(dst_addr);
    snd_addr.sin_port = htons(dst_port);

    ret = bind(m_sockfd,
            (struct sockaddr*)&recv_addr,
            sizeof(recv_addr));
    if (ret < 0)
    {
        m_error = UdpNodeError::BindFail;
        std::cerr << std::format(
            "UdpNode::initialize: Socket bind failed\n"
            "ret: {}\n",
            ret)
            << std::endl;
        goto UdpNode_initialize_exit;
    }

    ret = connect(m_sockfd,
            (struct sockaddr*)&snd_addr,
            sizeof(snd_addr));
    if (ret < 0)
    {
        m_error = UdpNodeError::ConnectFail;
        std::cerr << std::format(
            "UdpNode::initialize: Socket connect failed\n"
            "ret: {}\n",
            ret)
            << std::endl;

        goto UdpNode_initialize_exit;
    }

    m_error = UdpNodeError::None;
    std::cout << std::format(
        "UdpNode::initialize: Socket initialized successfully\n"
        "m_sockfd: {}, src 0x{:08X}:{}, dst 0x{:08X}:{}\n",
        m_sockfd, src_addr, src_port, dst_addr, dst_port)
        << std::endl;

UdpNode_initialize_exit:
    // Done
}

ssize_t
UdpNode::send(const uint8_t* data, size_t length)
{
    ssize_t sent_bytes = -1;

    sent_bytes = sendto(m_sockfd,
                        data,
                        length,
                        0,
                        nullptr,
                        0);
    
    if (sent_bytes < 0)
    {
        m_error = UdpNodeError::SendFail;
        std::cerr << std::format(
            "UdpNode::send: Send failed\n"
            "sent_bytes: {}\n",
            sent_bytes)
            << std::endl;
    }
    else
    {
        m_error = UdpNodeError::None;
    }

    return sent_bytes;
}


ssize_t
UdpNode::receive(uint8_t* buffer, size_t length)
{
    ssize_t recv_bytes = -1;

    recv_bytes = recvfrom(m_sockfd,
                          buffer,
                          length,
                          0,
                          nullptr,
                          nullptr);

    if (recv_bytes < 0)
    {
        m_error = UdpNodeError::RecvFail;
    }
    else
    {
        m_error = UdpNodeError::None;
    }

    return recv_bytes;
}


int
UdpNode::getFd(void) const
{
    return m_sockfd;
}


void
UdpNode::close(void)
{
    if (m_sockfd >= 0)
    {
        ::close(m_sockfd);
        m_sockfd = -1;
    }
}


UdpNode::UdpNodeError
UdpNode::getError(void) const
{
    return m_error;
}
