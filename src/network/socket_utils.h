#pragma once

/**
 * @file socket_utils.h
 * @brief Socket utility functions for creating and configuring TCP sockets
 * 
 * Provides low-level socket operations optimized for low latency with
 * TCP_NODELAY, optimized buffer sizes, and non-blocking I/O.
 */

#include <memory>
#include <string>

/**
 * @brief Smart pointer for socket file descriptors
 * 
 * Automatically closes socket when last reference is destroyed.
 */
using SocketPtr = std::shared_ptr<int>;

/**
 * @brief Sets socket to non-blocking mode
 * 
 * @param fd Socket file descriptor
 * @return true on success, false on error
 */
bool makeNonBlocking(int fd);

/**
 * @brief Creates and configures TCP server socket on port 8080
 * 
 * Configures: SO_REUSEADDR, TCP_NODELAY, 64KB buffers, backlog 128, non-blocking.
 * 
 * @return SocketPtr on success, nullptr on error
 */
SocketPtr startServer();

/**
 * @brief Creates TCP client socket and connects to localhost:8080
 * 
 * Blocking connect. For non-blocking with timeout, use clientConnectThread().
 * 
 * @return SocketPtr on success, nullptr on error
 */
SocketPtr startClient();
