/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file AppPacket.cpp
 * @ingroup app
 * @class AppPacket
 * @brief Application packet for UDP communication
 *
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <cstring>
#include <chrono>
#include <iostream>
#include <format>

#include "app/AppPacket.hpp"


/*******************************************************************************
 * Constant
 ******************************************************************************/
static constexpr size_t HEADER_SIZE = sizeof(AppPacketHeader);
static constexpr size_t FOOTER_SIZE = sizeof(AppPacketFooter);

/* CRC32 polynomial (IEEE 802.3) */
static constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320U;


/*******************************************************************************
 * Constructor/Destructor
 ******************************************************************************/
AppPacket::AppPacket()
{
    /* TX fields */
    m_unique_id   = 0U;
    m_lifesign    = 0U;
    m_data_ptr    = nullptr;
    m_data_length = 0U;
    m_crc32       = 0U;

    /* RX lifesign monitoring */
    m_rx_lifesign      = 0U;
    m_rx_lifesign_prev = 0U;
    m_last_change_time = std::chrono::steady_clock::now();
    m_last_recv_time   = std::chrono::steady_clock::now();
    m_comm_timeout_ms  = APP_PACKET_COMM_TIMEOUT_MS;

    /* Stability monitoring */
    m_expected_interval_ms = APP_PACKET_EXPECTED_INTERVAL_MS;
    m_tolerance_us         = APP_PACKET_INTERVAL_TOLERANCE_US;
    m_last_interval_us     = 0U;
    m_unstable_counter     = 0U;
    m_comm_unstable        = false;

    m_error = AppPacketError::None;
}

AppPacket::~AppPacket()
{
    /* Data pointer is not owned, do not delete */
}


/*******************************************************************************
 * Function Definition
 ******************************************************************************/

/**
 * @brief Set the unique packet identifier
 *
 * @param[in] id  Unique identifier value
 */
void
AppPacket::setUniqueId(uint32_t id)
{
    m_unique_id = id;
}

/**
 * @brief Set the data pointer and length for the payload
 *
 * @param[in] data    Pointer to payload data (not copied, must remain valid)
 * @param[in] length  Length of payload data in bytes
 */
void
AppPacket::setDataPointer(const uint8_t* data, size_t length)
{
    if (data == nullptr)
    {
        m_error = AppPacketError::InvalidDataPointer;
        m_data_ptr    = nullptr;
        m_data_length = 0U;
    }
    else if (length > APP_PACKET_MAX_DATA_SIZE)
    {
        m_error = AppPacketError::DataTooLarge;
        m_data_ptr    = nullptr;
        m_data_length = 0U;
    }
    else
    {
        m_error       = AppPacketError::None;
        m_data_ptr    = data;
        m_data_length = length;
    }
}

/**
 * @brief Encode the packet into a byte buffer for transmission
 *
 * Packet format:
 *   [Header: unique_id(4) + lifesign(2) + data_length(2)]
 *   [Payload: data(N)]
 *   [Footer: crc32(4)]
 *
 * @param[out] buffer       Output buffer to write encoded packet
 * @param[in]  buffer_size  Size of output buffer in bytes
 * @return Number of bytes written, or 0 on error
 */
size_t
AppPacket::encode(uint8_t* buffer, size_t buffer_size)
{
    size_t total_size = 0U;
    size_t offset     = 0U;
    AppPacketHeader header = {0};
    AppPacketFooter footer = {0};

    total_size = HEADER_SIZE + m_data_length + FOOTER_SIZE;

    if (buffer == nullptr)
    {
        m_error = AppPacketError::InvalidDataPointer;
        total_size = 0U;
        goto AppPacket_encode_exit;
    }

    if (buffer_size < total_size)
    {
        m_error = AppPacketError::BufferTooSmall;
        total_size = 0U;
        goto AppPacket_encode_exit;
    }

    /* Build header */
    header.unique_id   = m_unique_id;
    header.lifesign    = m_lifesign;
    header.data_length = static_cast<uint16_t>(m_data_length);

    /* Copy header to buffer */
    std::memmove(&buffer[offset], &header, HEADER_SIZE);
    offset += HEADER_SIZE;

    /* Copy payload data */
    if ((m_data_ptr != nullptr) && (m_data_length > 0U))
    {
        std::memmove(&buffer[offset], m_data_ptr, m_data_length);
        offset += m_data_length;
    }

    /* Calculate CRC32 over header + payload */
    m_crc32 = calculateCrc32(buffer, offset);
    footer.crc32 = m_crc32;

    /* Copy footer to buffer */
    std::memmove(&buffer[offset], &footer, FOOTER_SIZE);
    offset += FOOTER_SIZE;

    m_error = AppPacketError::None;
    total_size = offset;

    /* Auto-increment TX lifesign for next packet */
    m_lifesign++;

AppPacket_encode_exit:
    return total_size;
}

/**
 * @brief Decode a received byte buffer into the packet fields
 *
 * @param[in] buffer       Input buffer containing received packet
 * @param[in] buffer_size  Size of input buffer in bytes
 * @return true on success, false on error
 */
bool
AppPacket::decode(const uint8_t* buffer, size_t buffer_size)
{
    bool result = false;
    size_t offset = 0U;
    AppPacketHeader header = {0};
    AppPacketFooter footer = {0};
    uint32_t computed_crc = 0U;
    size_t expected_size = 0U;

    if (buffer == nullptr)
    {
        m_error = AppPacketError::InvalidDataPointer;
        goto AppPacket_decode_exit;
    }

    if (buffer_size < (HEADER_SIZE + FOOTER_SIZE))
    {
        m_error = AppPacketError::InvalidPacket;
        goto AppPacket_decode_exit;
    }

    /* Extract header */
    std::memmove(&header, &buffer[offset], HEADER_SIZE);
    offset += HEADER_SIZE;

    /* Validate packet size */
    expected_size = HEADER_SIZE + header.data_length + FOOTER_SIZE;
    if (buffer_size < expected_size)
    {
        m_error = AppPacketError::InvalidPacket;
        goto AppPacket_decode_exit;
    }

    if (header.data_length > APP_PACKET_MAX_DATA_SIZE)
    {
        m_error = AppPacketError::DataTooLarge;
        goto AppPacket_decode_exit;
    }

    /* Store header fields */
    m_unique_id   = header.unique_id;
    m_data_length = header.data_length;

    /* Update received lifesign monitoring */
    updateReceivedLifesign(header.lifesign);

    /* Point to payload in buffer (not copied) */
    if (m_data_length > 0U)
    {
        m_data_ptr = &buffer[offset];
    }
    else
    {
        m_data_ptr = nullptr;
    }
    offset += m_data_length;

    /* Extract footer */
    std::memmove(&footer, &buffer[offset], FOOTER_SIZE);
    m_crc32 = footer.crc32;

    /* Verify CRC32 */
    computed_crc = calculateCrc32(buffer, HEADER_SIZE + m_data_length);
    if (computed_crc != m_crc32)
    {
        m_error = AppPacketError::CrcMismatch;
        goto AppPacket_decode_exit;
    }

    m_error = AppPacketError::None;
    result = true;

AppPacket_decode_exit:
    return result;
}

/**
 * @brief Get the unique packet identifier
 */
uint32_t
AppPacket::getUniqueId(void) const
{
    return m_unique_id;
}

/**
 * @brief Get the lifesign counter value
 */
uint16_t
AppPacket::getLifesign(void) const
{
    return m_lifesign;
}

/**
 * @brief Get the last received lifesign value
 */
uint16_t
AppPacket::getReceivedLifesign(void) const
{
    return m_rx_lifesign;
}

/**
 * @brief Get the pointer to payload data
 */
const uint8_t*
AppPacket::getData(void) const
{
    return m_data_ptr;
}

/**
 * @brief Get the payload data length
 */
size_t
AppPacket::getDataLength(void) const
{
    return m_data_length;
}

/**
 * @brief Get the CRC32 checksum
 */
uint32_t
AppPacket::getCrc32(void) const
{
    return m_crc32;
}

/**
 * @brief Get the current error state
 */
AppPacket::AppPacketError
AppPacket::getError(void) const
{
    return m_error;
}

/**
 * @brief Update received lifesign and assess communication status
 *
 * Call this after decoding a packet to track if the remote peer's
 * lifesign is incrementing (alive) or frozen (loss of communication),
 * and to assess timing stability.
 *
 * @param[in] lifesign  Lifesign value from received packet
 */
void
AppPacket::updateReceivedLifesign(uint16_t lifesign)
{
    auto now = std::chrono::steady_clock::now();
    uint32_t expected_us = m_expected_interval_ms * 1000U;
    uint32_t lower_bound = 0U;
    uint32_t upper_bound = 0U;

    /* Calculate interval since last receive */
    m_last_interval_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now - m_last_recv_time).count());
    m_last_recv_time = now;

    /* Check interval stability */
    if (expected_us > m_tolerance_us)
    {
        lower_bound = expected_us - m_tolerance_us;
    }
    upper_bound = expected_us + m_tolerance_us;

    if ((m_last_interval_us < lower_bound) || (m_last_interval_us > upper_bound))
    {
        /* Out of tolerance */
        if (m_unstable_counter < UINT16_MAX)
        {
            m_unstable_counter++;
        }
        m_comm_unstable = true;

        if (m_error == AppPacketError::None)
        {
            m_error = AppPacketError::UnstableCommunication;
        }
    }
    else
    {
        /* Within tolerance - communication is stable */
        m_unstable_counter = 0U;
        m_comm_unstable = false;

        if (m_error == AppPacketError::UnstableCommunication)
        {
            m_error = AppPacketError::None;
        }
    }

    /* Update lifesign tracking */
    m_rx_lifesign_prev = m_rx_lifesign;
    m_rx_lifesign      = lifesign;

    if (m_rx_lifesign != m_rx_lifesign_prev)
    {
        /* Lifesign changed - peer is alive, update timestamp */
        m_last_change_time = now;

        if (m_error == AppPacketError::LossOfCommunication)
        {
            m_error = AppPacketError::None;
        }
    }
    /* Note: isCommLost() checks elapsed time to determine loss of communication */
}

/**
 * @brief Check if communication is lost (lifesign frozen for too long)
 *
 * @return true if time since last lifesign change exceeds timeout, false otherwise
 */
bool
AppPacket::isCommLost(void) const
{
    bool lost = false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - m_last_change_time).count();

    if (static_cast<uint32_t>(elapsed_ms) >= m_comm_timeout_ms)
    {
        lost = true;
    }

    return lost;
}

/**
 * @brief Check if communication timing is unstable
 *
 * @return true if last interval was outside expected range ± tolerance
 */
bool
AppPacket::isCommUnstable(void) const
{
    return m_comm_unstable;
}

/**
 * @brief Get the last measured receive interval
 *
 * @return Last interval in microseconds
 */
uint32_t
AppPacket::getLastIntervalUs(void) const
{
    return m_last_interval_us;
}

/**
 * @brief Get the expected receive interval
 *
 * @return Expected interval in milliseconds
 */
uint32_t
AppPacket::getExpectedIntervalMs(void) const
{
    return m_expected_interval_ms;
}

/**
 * @brief Get the interval tolerance
 *
 * @return Tolerance in microseconds
 */
uint32_t
AppPacket::getIntervalToleranceUs(void) const
{
    return m_tolerance_us;
}

/**
 * @brief Get the consecutive unstable interval count
 *
 * @return Number of consecutive out-of-tolerance intervals
 */
uint16_t
AppPacket::getUnstableCounter(void) const
{
    return m_unstable_counter;
}

/**
 * @brief Get time elapsed since last lifesign change
 *
 * @return Elapsed time in milliseconds
 */
uint32_t
AppPacket::getTimeSinceLastChange(void) const
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - m_last_change_time).count();

    return static_cast<uint32_t>(elapsed_ms);
}

/**
 * @brief Get the configured communication timeout
 *
 * @return Timeout in milliseconds
 */
uint32_t
AppPacket::getCommTimeout(void) const
{
    return m_comm_timeout_ms;
}

/**
 * @brief Set the communication timeout
 *
 * @param[in] timeout_ms  Timeout in milliseconds to declare loss of communication
 */
void
AppPacket::setCommTimeout(uint32_t timeout_ms)
{
    m_comm_timeout_ms = timeout_ms;
}

/**
 * @brief Set the expected receive interval and tolerance
 *
 * @param[in] interval_ms   Expected interval between packets (ms)
 * @param[in] tolerance_us  Allowed deviation from expected interval (us)
 */
void
AppPacket::setExpectedInterval(uint32_t interval_ms, uint32_t tolerance_us)
{
    m_expected_interval_ms = interval_ms;
    m_tolerance_us         = tolerance_us;
}

/**
 * @brief Reset the communication monitor (e.g., on reconnect)
 */
void
AppPacket::resetCommMonitor(void)
{
    auto now = std::chrono::steady_clock::now();

    m_rx_lifesign      = 0U;
    m_rx_lifesign_prev = 0U;
    m_last_change_time = now;
    m_last_recv_time   = now;

    /* Reset stability tracking */
    m_last_interval_us = 0U;
    m_unstable_counter = 0U;
    m_comm_unstable    = false;

    if ((m_error == AppPacketError::LossOfCommunication) ||
        (m_error == AppPacketError::UnstableCommunication))
    {
        m_error = AppPacketError::None;
    }
}

/**
 * @brief Calculate CRC32 checksum (IEEE 802.3 polynomial)
 *
 * @param[in] data    Pointer to data
 * @param[in] length  Length of data in bytes
 * @return CRC32 checksum value
 */
uint32_t
AppPacket::calculateCrc32(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t idx = 0U; idx < length; idx++)
    {
        crc ^= static_cast<uint32_t>(data[idx]);

        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ CRC32_POLYNOMIAL;
            }
            else
            {
                crc = crc >> 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFU;
}
