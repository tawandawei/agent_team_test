/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file UdpNode.hpp
 * @ingroup socket
 * @class UdpHandle
 * @brief UDP socket handler
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_SOCKET_UDPNODE_HPP
#define AGENT_TEAM_TEST_SOCKET_UDPNODE_HPP
/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <string_view>
#include <cstdint>


/*******************************************************************************
 * Macro
 ******************************************************************************/



/*******************************************************************************
 * Enum / Structure
 ******************************************************************************/



/*******************************************************************************
 * Class Declaration
 ******************************************************************************/
class UdpNode
{
/***********************************************************
 * Enum
 **********************************************************/
public:
    enum class UdpNodeError
    {
        None,
        SocketCreateFail,
        BindFail,
        ConnectFail,
        SendFail,
        RecvFail
    };

/***********************************************************
 * Constructor/Destructor
 **********************************************************/
public:
    UdpNode();
    ~UdpNode();

/***********************************************************
 * Method
 **********************************************************/
public:
    void initialize(uint32_t src_addr, uint16_t src_port,
                    uint32_t dst_addr, uint16_t dst_port);
    ssize_t send(const uint8_t* data, size_t length);
    ssize_t receive(uint8_t* buffer, size_t length);
    int getFd(void) const;

    void close(void);
    UdpNode::UdpNodeError getError(void) const;

private:
/***********************************************************
 * Helper Method
 **********************************************************/

/***********************************************************
 * Data
 **********************************************************/
private:
    /* Specific property */
    int m_sockfd;
    UdpNode::UdpNodeError m_error;
};


#endif  // AGENT_TEAM_TEST_SOCKET_UDPNODE_HPP
