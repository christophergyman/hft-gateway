#pragma once

#include "socket_utils.h"
#include "connection.h"
#include "message.h"
#include <atomic>
#include <vector>
#include <mutex>

// Thread function to receive messages from client (server side)
void serverReceiveThread(ClientConnectionPtr clientConn);

// Non-blocking server accept in background thread with poll
void serverAcceptThread(SocketPtr serverSocket, 
                        std::atomic<bool>& running, 
                        std::vector<ClientConnectionPtr>& clients,
                        std::mutex& clientsMutex, 
                        std::atomic<int>& nextClientId,
                        size_t maxConnections = 1000);
