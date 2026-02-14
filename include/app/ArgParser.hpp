/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file ArgParser.hpp
 * @ingroup app
 * 
 * @brief Command-line argument parser
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_APP_ARGPARSER_HPP
#define AGENT_TEAM_TEST_APP_ARGPARSER_HPP
/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <cstdint>
#include <arpa/inet.h>


/*******************************************************************************
 * Macro
 ******************************************************************************/



/*******************************************************************************
 * Enum / Structure
 ******************************************************************************/
struct UdpPeerArgs
{
    uint32_t src_addr;
    uint16_t src_port;
    uint32_t dst_addr;
    uint16_t dst_port;
};


/*******************************************************************************
 * Function Declaration
 ******************************************************************************/
bool parseUdpPeerArgs(int argc, char* argv[], UdpPeerArgs& args);


#endif  // AGENT_TEAM_TEST_APP_ARGPARSER_HPP
