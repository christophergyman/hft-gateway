#include "message.h"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>

// Global message queue instance
MessageQueue receivedMessages;

// MessageBuffer implementation
bool MessageBuffer::addData(const char* data, size_t len) {
    buffer_.append(data, len);
    return true;
}

bool MessageBuffer::extractMessage(std::string& message) {
    if (buffer_.size() < 4) {
        return false; // Need at least 4 bytes for length header
    }
    
    // Read length (network byte order)
    uint32_t length;
    std::memcpy(&length, buffer_.data(), 4);
    length = ntohl(length);
    
    if (length > 1024 * 1024) { // Sanity check: max 1MB
        buffer_.clear();
        return false;
    }
    
    if (buffer_.size() < 4 + length) {
        return false; // Not enough data yet
    }
    
    // Extract message
    message = buffer_.substr(4, length);
    buffer_.erase(0, 4 + length);
    return true;
}

void MessageBuffer::clear() {
    buffer_.clear();
}

// MessageQueue implementation
void MessageQueue::push(const std::string& source, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push({source, message});
}

bool MessageQueue::pop(std::string& source, std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (messages_.empty()) {
        return false;
    }
    auto msg = messages_.front();
    messages_.pop();
    source = msg.first;
    message = msg.second;
    return true;
}

void MessageQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!messages_.empty()) {
        messages_.pop();
    }
}

// Send message with length prefix (framed protocol)
bool sendFramedMessage(int socketFd, const std::string& message) {
    if (socketFd < 0 || message.empty()) {
        return false;
    }
    
    // Prepare framed message: [4 bytes length][message data]
    uint32_t length = htonl(static_cast<uint32_t>(message.size()));
    std::vector<char> framed(4 + message.size());
    std::memcpy(framed.data(), &length, 4);
    std::memcpy(framed.data() + 4, message.data(), message.size());
    
    size_t bytesSent = 0;
    const size_t totalLength = framed.size();
    
    while (bytesSent < totalLength) {
        // Use poll to wait for write readiness
        struct pollfd pfd;
        pfd.fd = socketFd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        
        int pollResult = poll(&pfd, 1, 100); // 100ms timeout
        if (pollResult < 0) {
            return false; // Error
        }
        if (pollResult == 0) {
            continue; // Timeout, check again
        }
        
        if (!(pfd.revents & POLLOUT)) {
            continue; // Not ready for writing
        }
        
        // Send data
        #ifdef MSG_NOSIGNAL
        const ssize_t sent = send(socketFd, framed.data() + bytesSent, 
                                  totalLength - bytesSent, MSG_NOSIGNAL);
        #else
        const ssize_t sent = send(socketFd, framed.data() + bytesSent, 
                                  totalLength - bytesSent, 0);
        #endif
        
        if (sent <= 0) {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue; // Would block, poll again
            }
            return false; // Error or connection closed
        }
        bytesSent += static_cast<size_t>(sent);
    }
    return true;
}

bool sendToClient(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message) {
    if (!clientSocket || *clientSocket < 0 || !message || message->empty()) {
        return false;
    }
    return sendFramedMessage(*clientSocket, *message);
}

bool sendToServer(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message) {
    if (!clientSocket || *clientSocket < 0 || !message || message->empty()) {
        return false;
    }
    return sendFramedMessage(*clientSocket, *message);
}

bool receiveFramedMessage(int socketFd, MessageBuffer& buffer, std::string& message) {
    if (socketFd < 0) {
        return false;
    }
    
    // Use poll to check for data
    struct pollfd pfd;
    pfd.fd = socketFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int pollResult = poll(&pfd, 1, 100); // 100ms timeout
    if (pollResult <= 0) {
        return false; // Timeout or error
    }
    
    if (!(pfd.revents & POLLIN)) {
        return false; // No data available
    }
    
    char recvBuffer[1024];
    ssize_t bytesReceived = recv(socketFd, recvBuffer, sizeof(recvBuffer), 0);
    
    if (bytesReceived <= 0) {
        if (bytesReceived == 0) {
            // Connection closed
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Would block, not an error
            return false;
        }
        return false; // Error
    }
    
    // Add received data to buffer
    buffer.addData(recvBuffer, bytesReceived);
    
    // Try to extract complete message
    return buffer.extractMessage(message);
}

bool receiveFromClient(const SocketPtr& clientSocket, std::string& message) {
    static MessageBuffer buffer;
    if (!clientSocket || *clientSocket < 0) {
        return false;
    }
    return receiveFramedMessage(*clientSocket, buffer, message);
}

bool receiveFromServer(const SocketPtr& clientSocket, std::string& message) {
    static MessageBuffer buffer;
    if (!clientSocket || *clientSocket < 0) {
        return false;
    }
    return receiveFramedMessage(*clientSocket, buffer, message);
}
