#pragma once

/**
 * @file connection.h
 * @brief Client connection management structures
 */

#include "socket_utils.h"
#include "message.h"
#include <thread>
#include <atomic>

/**
 * @struct ClientConnection
 * @brief Represents a single client connection with socket, thread, and buffer
 * 
 * Thread safety: Protect with mutexes when accessed from multiple threads.
 */
struct ClientConnection {
    SocketPtr socket;                    ///< Socket pointer
    std::thread receiveThread;            ///< Receive thread handle
    std::atomic<bool> running{false};     ///< Thread should continue
    std::atomic<bool> connected{false};   ///< Connection is active
    MessageBuffer buffer;                 ///< Per-connection message buffer
    int id;                               ///< Unique client identifier
    
    ClientConnection(int clientId);
    ~ClientConnection();
};

using ClientConnectionPtr = std::shared_ptr<ClientConnection>;
