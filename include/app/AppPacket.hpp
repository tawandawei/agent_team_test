/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file AppPacket.hpp
 * @ingroup app
 * @class AppPacket
 * @brief Application packet for UDP communication
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_APP_APPPACKET_HPP
#define AGENT_TEAM_TEST_APP_APPPACKET_HPP
/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <cstdint>
#include <cstddef>
#include <chrono>


/*******************************************************************************
 * Macro
 ******************************************************************************/
static constexpr size_t   APP_PACKET_MAX_DATA_SIZE         = 256U;
static constexpr uint32_t APP_PACKET_COMM_TIMEOUT_MS       = 1000U;  /**< Default communication timeout (ms) */
static constexpr uint32_t APP_PACKET_EXPECTED_INTERVAL_MS  = 100U;   /**< Default expected receive interval (ms) */
static constexpr uint32_t APP_PACKET_INTERVAL_TOLERANCE_US = 5000U;  /**< Default tolerance (us) */


/*******************************************************************************
 * Enum / Structure
 ******************************************************************************/

/**
 * @brief Packet header structure (wire format)
 */
struct AppPacketHeader
{
    uint32_t unique_id;     /**< Unique packet identifier */
    uint16_t lifesign;      /**< Lifesign counter */
    uint16_t data_length;   /**< Length of payload data */
};

/**
 * @brief Packet footer structure (wire format)
 */
struct AppPacketFooter
{
    uint32_t crc32;         /**< CRC32 checksum */
};


/*******************************************************************************
 * Class Declaration
 ******************************************************************************/
class AppPacket
{
/***********************************************************
 * Enum
 **********************************************************/
public:
    enum class AppPacketError
    {
        None,
        InvalidDataPointer,
        DataTooLarge,
        BufferTooSmall,
        InvalidPacket,
        CrcMismatch,
        UnstableCommunication,
        LossOfCommunication
    };

/***********************************************************
 * Constructor/Destructor
 **********************************************************/
public:
    AppPacket();
    ~AppPacket();

/***********************************************************
 * Method
 **********************************************************/
public:
    /* Transmit packet management */
    void setUniqueId(uint32_t id);
    void setDataPointer(const uint8_t* data, size_t length);
    size_t encode(uint8_t* buffer, size_t buffer_size);

    /* Receive packet management */
    bool decode(const uint8_t* buffer, size_t buffer_size);
    void updateReceivedLifesign(uint16_t lifesign);
    bool isCommLost(void) const;
    bool isCommUnstable(void) const;
    void setCommTimeout(uint32_t timeout_ms);
    void setExpectedInterval(uint32_t interval_ms, uint32_t tolerance_us);
    void resetCommMonitor(void);
    uint32_t getTimeSinceLastChange(void) const;
    uint32_t getLastIntervalUs(void) const;

    uint32_t getUniqueId(void) const;
    uint16_t getLifesign(void) const;           /**< Get TX lifesign */
    uint16_t getReceivedLifesign(void) const;   /**< Get last RX lifesign */
    uint32_t getCommTimeout(void) const;         /**< Get configured timeout (ms) */
    uint32_t getExpectedIntervalMs(void) const;  /**< Get expected interval (ms) */
    uint32_t getIntervalToleranceUs(void) const; /**< Get tolerance (us) */
    uint16_t getUnstableCounter(void) const;     /**< Get consecutive unstable count */
    const uint8_t* getData(void) const;
    size_t getDataLength(void) const;
    uint32_t getCrc32(void) const;
    AppPacket::AppPacketError getError(void) const;

/***********************************************************
 * Helper Method
 **********************************************************/
private:
    static uint32_t calculateCrc32(const uint8_t* data, size_t length);

/***********************************************************
 * Data
 **********************************************************/
private:
    /* TX packet fields */
    uint32_t m_unique_id;
    uint16_t m_lifesign;            /**< TX lifesign (auto-incremented on encode) */
    const uint8_t* m_data_ptr;
    size_t m_data_length;
    uint32_t m_crc32;

    /* RX lifesign monitoring */
    uint16_t m_rx_lifesign;         /**< Last received lifesign */
    uint16_t m_rx_lifesign_prev;    /**< Previous received lifesign */
    std::chrono::steady_clock::time_point m_last_change_time;  /**< Time of last lifesign change */
    std::chrono::steady_clock::time_point m_last_recv_time;    /**< Time of last packet receive */
    uint32_t m_comm_timeout_ms;     /**< Timeout to declare loss of communication (ms) */

    /* Stability monitoring */
    uint32_t m_expected_interval_ms;  /**< Expected receive interval (ms) */
    uint32_t m_tolerance_us;          /**< Allowed tolerance (us) */
    uint32_t m_last_interval_us;      /**< Last measured interval (us) */
    uint16_t m_unstable_counter;      /**< Consecutive out-of-tolerance count */
    bool     m_comm_unstable;         /**< Current stability status */

    AppPacket::AppPacketError m_error;
};


#endif  // AGENT_TEAM_TEST_APP_APPPACKET_HPP
