#pragma once

/**
 * @file client.h
 * @brief Client-side thread functions for connecting and receiving from server
 */

#include "../network/socket_utils.h"
#include "../network/message.h"
#include <atomic>
#include <string>

/**
 * @brief Receives messages from server (runs in dedicated thread)
 * 
 * Pushes messages to receivedMessages queue. Detects disconnections via poll().
 */
void clientReceiveThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running, 
                        std::atomic<bool>& connected, 
                        MessageBuffer& buffer);

/**
 * @brief Non-blocking connection with timeout (runs in dedicated thread)
 * 
 * Configures socket for low latency and uses poll() to wait for connection.
 */
void clientConnectThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running,
                        std::atomic<bool>& connectComplete, 
                        bool& connectSuccess,
                        const std::string& serverAddr = "127.0.0.1", 
                        int timeoutSeconds = 5);
