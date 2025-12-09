#pragma once

#include "socket_utils.h"
#include <string>
#include <memory>
#include <queue>
#include <mutex>

// Message buffer for handling length-prefixed messages
class MessageBuffer {
public:
    bool addData(const char* data, size_t len);
    bool extractMessage(std::string& message);
    void clear();

private:
    std::string buffer_;
};

// Thread-safe message queue for received messages
class MessageQueue {
public:
    void push(const std::string& source, const std::string& message);
    bool pop(std::string& source, std::string& message);
    void clear();

private:
    std::queue<std::pair<std::string, std::string>> messages_;
    std::mutex mutex_;
};

// Global message queue instance
extern MessageQueue receivedMessages;

// Send message with length prefix (framed protocol)
bool sendFramedMessage(int socketFd, const std::string& message);

// Send message from server to client
bool sendToClient(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message);

// Send message from client to server
bool sendToServer(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message);

// Receive framed message using buffer
bool receiveFramedMessage(int socketFd, MessageBuffer& buffer, std::string& message);

// Receive message from client (server side) - legacy wrapper
bool receiveFromClient(const SocketPtr& clientSocket, std::string& message);

// Receive message from server (client side) - legacy wrapper
bool receiveFromServer(const SocketPtr& clientSocket, std::string& message);
