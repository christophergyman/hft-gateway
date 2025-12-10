#pragma once

/**
 * @file message.h
 * @brief Message framing, buffering, and transmission utilities
 * 
 * Implements length-prefixed message protocol with optimized buffering
 * for partial reads/writes and thread-safe message queues.
 */

#include "socket_utils.h"
#include <string>
#include <memory>
#include <queue>
#include <mutex>

/**
 * @class MessageBuffer
 * @brief Buffers length-prefixed messages for partial reads
 * 
 * Uses read position tracking to avoid memory copies. Optimized for high throughput.
 * Format: [4 bytes: length (network byte order)][N bytes: payload]
 * Max size: 1MB
 */
class MessageBuffer {
public:
    /**
     * @brief Adds received data to buffer (auto-compacts if needed)
     */
    bool addData(const char* data, size_t len);
    
    /**
     * @brief Extracts complete message if available
     * 
     * @param message Output parameter
     * @return true if complete message extracted, false if incomplete
     */
    bool extractMessage(std::string& message);
    
    /**
     * @brief Clears buffer and resets read position
     */
    void clear();

private:
    std::string buffer_;        ///< Internal buffer storing received data
    size_t readPos_ = 0;        ///< Current read position (avoids erase operations)
    
    /**
     * @brief Compacts buffer when readPos_ > half buffer size or buffer > 1MB
     */
    void compactIfNeeded();
};

/**
 * @class MessageQueue
 * @brief Thread-safe message queue for inter-thread communication
 */
class MessageQueue {
public:
    void push(const std::string& source, const std::string& message);
    bool pop(std::string& source, std::string& message);
    void clear();

private:
    std::queue<std::pair<std::string, std::string>> messages_;  ///< Internal message queue
    std::mutex mutex_;  ///< Mutex for thread-safe operations
};

/**
 * @brief Global message queue - receive threads push, main thread pops
 */
extern MessageQueue receivedMessages;

/**
 * @brief Sends length-prefixed message: [4 bytes: length][N bytes: payload]
 * 
 * Non-blocking send with 1ms poll timeout. Handles partial writes.
 */
bool sendFramedMessage(int socketFd, const std::string& message);

bool sendToClient(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message);
bool sendToServer(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message);

/**
 * @brief Receives and extracts complete framed message
 * 
 * Non-blocking receive with 1ms poll timeout. Handles partial reads.
 * Buffer should be per-connection.
 */
bool receiveFramedMessage(int socketFd, MessageBuffer& buffer, std::string& message);

/**
 * @deprecated Legacy wrapper - creates temporary buffer (inefficient).
 * Use receiveFramedMessage() with per-connection MessageBuffer instead.
 */
bool receiveFromClient(const SocketPtr& clientSocket, std::string& message);

/**
 * @deprecated Legacy wrapper - creates temporary buffer (inefficient).
 * Use receiveFramedMessage() with per-connection MessageBuffer instead.
 */
bool receiveFromServer(const SocketPtr& clientSocket, std::string& message);
