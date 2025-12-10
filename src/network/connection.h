#pragma once

#include "socket_utils.h"
#include "message.h"
#include <thread>
#include <atomic>

// Client connection structure for multi-client support
struct ClientConnection {
    SocketPtr socket;
    std::thread receiveThread;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    MessageBuffer buffer;
    int id;
    
    ClientConnection(int clientId);
    ~ClientConnection();
};

using ClientConnectionPtr = std::shared_ptr<ClientConnection>;
