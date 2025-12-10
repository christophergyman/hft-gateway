#pragma once

#include "../network/socket_utils.h"
#include "../network/message.h"
#include <atomic>
#include <string>

// Thread function to receive messages from server (client side)
void clientReceiveThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running, 
                        std::atomic<bool>& connected, 
                        MessageBuffer& buffer);

// Non-blocking client connect in background thread with timeout
void clientConnectThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running,
                        std::atomic<bool>& connectComplete, 
                        bool& connectSuccess,
                        const std::string& serverAddr = "127.0.0.1", 
                        int timeoutSeconds = 5);
