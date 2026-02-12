/* SPDX-License-Identifier: MIT License */
/*******************************************************************************
 *
 * This document and its contents are parts of the Agent Team Test project.
 * 
 * Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
 * 
 *//*!
 * @file LockFreeRingBuffer.hpp
 * @ingroup thread
 * @brief Lock-free SPSC (Single Producer Single Consumer) ring buffer
 *
 ******************************************************************************/
#ifndef AGENT_TEAM_TEST_THREAD_LOCKFREERINGBUFFER_HPP
#define AGENT_TEAM_TEST_THREAD_LOCKFREERINGBUFFER_HPP

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>

/*******************************************************************************
 * Template Class Declaration
 ******************************************************************************/

/**
 * @brief Lock-free ring buffer for UDP packets
 * 
 * Single Producer Single Consumer (SPSC) ring buffer optimized for
 * low-latency inter-thread communication. Cache-line aligned to prevent
 * false sharing between producer and consumer.
 */
template<size_t MaxPacketSize = 2048, size_t Capacity = 1024>
class LockFreeRingBuffer
{
public:
    struct Packet
    {
        uint16_t length;
        uint8_t data[MaxPacketSize];
    };

private:
    std::array<Packet, Capacity> m_buffer;
    
    // Cache line alignment to prevent false sharing
    alignas(64) std::atomic<size_t> m_writeIdx;
    alignas(64) std::atomic<size_t> m_readIdx;
    
public:
    LockFreeRingBuffer() : m_writeIdx(0), m_readIdx(0) {}
    
    /**
     * @brief Push packet to ring buffer (Producer)
     * 
     * @param data Pointer to packet data
     * @param length Length of packet data
     * @return true if successful, false if buffer is full
     */
    bool push(const uint8_t* data, size_t length)
    {
        if (length > MaxPacketSize)
        {
            return false;
        }
        
        size_t currentWrite = m_writeIdx.load(std::memory_order_relaxed);
        size_t nextWrite = (currentWrite + 1) % Capacity;
        
        // Check if buffer is full
        if (nextWrite == m_readIdx.load(std::memory_order_acquire))
        {
            return false;
        }
        
        // Write data
        m_buffer[currentWrite].length = static_cast<uint16_t>(length);
        std::memcpy(m_buffer[currentWrite].data, data, length);
        
        // Publish write
        m_writeIdx.store(nextWrite, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Pop packet from ring buffer (Consumer)
     * 
     * @param data Pointer to output buffer
     * @param maxLength Maximum length of output buffer
     * @param actualLength Actual length of packet read
     * @return true if successful, false if buffer is empty
     */
    bool pop(uint8_t* data, size_t maxLength, size_t& actualLength)
    {
        size_t currentRead = m_readIdx.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (currentRead == m_writeIdx.load(std::memory_order_acquire))
        {
            return false;
        }
        
        // Read data
        actualLength = m_buffer[currentRead].length;
        if (actualLength > maxLength)
        {
            return false;
        }
        
        std::memcpy(data, m_buffer[currentRead].data, actualLength);
        
        // Publish read
        m_readIdx.store((currentRead + 1) % Capacity, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Get current number of packets in buffer
     */
    size_t size() const
    {
        size_t w = m_writeIdx.load(std::memory_order_acquire);
        size_t r = m_readIdx.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (Capacity - r + w);
    }
    
    /**
     * @brief Check if buffer is empty
     */
    bool isEmpty() const
    {
        return m_readIdx.load(std::memory_order_acquire) == 
               m_writeIdx.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Check if buffer is full
     */
    bool isFull() const
    {
        size_t currentWrite = m_writeIdx.load(std::memory_order_acquire);
        size_t nextWrite = (currentWrite + 1) % Capacity;
        return nextWrite == m_readIdx.load(std::memory_order_acquire);
    }
};

#endif  // AGENT_TEAM_TEST_THREAD_LOCKFREERINGBUFFER_HPP
