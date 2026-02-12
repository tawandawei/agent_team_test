/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file ArgParser.cpp
 * @ingroup app
 * 
 * @brief Command-line argument parser
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <string>

#include "app/ArgParser.hpp"


/*******************************************************************************
 * Constant
 ******************************************************************************/
static constexpr uint8_t PARSE_FLAG_HAS_SRC       = 0x01U;
static constexpr uint8_t PARSE_FLAG_HAS_DST       = 0x02U;
static constexpr uint8_t PARSE_FLAG_ERR_SRC_FMT   = 0x04U;
static constexpr uint8_t PARSE_FLAG_ERR_DST_FMT   = 0x08U;
static constexpr uint8_t PARSE_FLAG_REQUIRED_MASK  = 0x03U;  /* HAS_SRC | HAS_DST */
static constexpr uint8_t PARSE_FLAG_ERROR_MASK     = 0x0CU;  /* ERR_SRC | ERR_DST */


/*******************************************************************************
 * Function Definition
 ******************************************************************************/

/**
 * @brief Parse an address:port token into a uint32_t address and uint16_t port
 *
 * @param[in]  token  String in the form <addr>:<port>
 * @param[out] addr   Parsed IPv4 address in host byte order
 * @param[out] port   Parsed port number
 * @return true on success, false on malformed input
 */
static bool
parseAddrPort(const std::string& token, uint32_t& addr, uint16_t& port)
{
    bool result = false;
    auto colon_pos = token.rfind(':');

    if (colon_pos != std::string::npos)
    {
        std::string addr_str = token.substr(0, colon_pos);
        struct in_addr in = {0};
        int pton_ret = inet_pton(AF_INET, addr_str.c_str(), &in);

        if (pton_ret == 1)
        {
            addr   = ntohl(in.s_addr);
            port   = static_cast<uint16_t>(
                         std::stoul(token.substr(colon_pos + 1U)));
            result = true;
        }
    }

    return result;
}

/**
 * @brief Parse --src and --dst arguments in the form <addr>:<port>
 *
 * Expected usage:
 *   --src <own_addr>:<port> --dst <remote_addr>:<port>
 *
 * @param[in]  argc  Argument count
 * @param[in]  argv  Argument vector
 * @param[out] args  Parsed source / destination addresses and ports
 * @return true on success, false on missing or malformed arguments
 */
bool
parseUdpPeerArgs(int argc, char* argv[], UdpPeerArgs& args)
{
    bool    result = false;
    uint8_t result_flags  = 0x00U;
    int     next   = 0;

    for (int idx = 1; idx < argc; idx ++)
    {
        next = idx + 1;

        if ((std::strcmp(argv[idx], "--src") == 0) &&
            (next < argc))
        {
            idx ++;  // Move to the argument after --src (pass white space)
            std::string token = argv[idx];

            if (parseAddrPort(token, args.src_addr, args.src_port) == true)
            {
                result_flags |= PARSE_FLAG_HAS_SRC;
            }
            else
            {
                result_flags |= PARSE_FLAG_ERR_SRC_FMT;
            }
        }
        else if ((std::strcmp(argv[idx], "--dst") == 0) &&
                 (next < argc))
        {
            idx ++;  // Move to the argument after --dst (pass white space)
            std::string token = argv[idx];

            if (parseAddrPort(token, args.dst_addr, args.dst_port) == true)
            {
                result_flags |= PARSE_FLAG_HAS_DST;
            }
            else
            {
                result_flags |= PARSE_FLAG_ERR_DST_FMT;
            }
        }
        else
        {
            /* Unrecognized argument, skip */
        }
    }

    /* Report errors at the end */
    if ((result_flags & PARSE_FLAG_ERR_SRC_FMT) != 0x00U)
    {
        std::cerr << "Error: invalid --src format, expected <addr>:<port>"
                  << std::endl;
    }

    if ((result_flags & PARSE_FLAG_ERR_DST_FMT) != 0x00U)
    {
        std::cerr << "Error: invalid --dst format, expected <addr>:<port>"
                  << std::endl;
    }

    if ((result_flags & PARSE_FLAG_HAS_SRC) == 0x00U)
    {
        std::cerr << "Error: missing --src <addr>:<port>" << std::endl;
    }

    if ((result_flags & PARSE_FLAG_HAS_DST) == 0x00U)
    {
        std::cerr << "Error: missing --dst <addr>:<port>" << std::endl;
    }

    if (((result_flags & PARSE_FLAG_REQUIRED_MASK) == PARSE_FLAG_REQUIRED_MASK) &&
        ((result_flags & PARSE_FLAG_ERROR_MASK) == 0x00U))
    {
        result = true;
    }

    return result;
}
