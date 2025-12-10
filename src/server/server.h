#pragma once

/**
 * @file server.h
 * @brief Server-side thread functions for accepting and receiving from clients
 */

#include "../network/socket_utils.h"
#include "../network/connection.h"
#include "../network/message.h"
#include <atomic>
#include <vector>
#include <mutex>

/**
 * @brief Receives messages from client connection (runs in dedicated thread)
 * 
 * Pushes messages to receivedMessages queue. Detects disconnections via poll().
 */
void serverReceiveThread(ClientConnectionPtr clientConn);

/**
 * @brief Accepts new client connections (runs in dedicated thread)
 * 
 * Non-blocking accept loop with 1ms poll timeout. Configures sockets for low latency.
 * Cleans up disconnected clients and enforces connection limits.
 */
void serverAcceptThread(SocketPtr serverSocket, 
                        std::atomic<bool>& running, 
                        std::vector<ClientConnectionPtr>& clients,
                        std::mutex& clientsMutex, 
                        std::atomic<int>& nextClientId,
                        size_t maxConnections = 1000);
