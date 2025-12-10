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
    // Compact if buffer is getting large and we've read a significant portion
    compactIfNeeded();
    buffer_.append(data, len);
    return true;
}

bool MessageBuffer::extractMessage(std::string& message) {
    const size_t available = buffer_.size() - readPos_;
    if (available < 4) {
        return false; // Need at least 4 bytes for length header
    }
    
    // Read length (network byte order) from current read position
    uint32_t length;
    std::memcpy(&length, buffer_.data() + readPos_, 4);
    length = ntohl(length);
    
    if (length > 1024 * 1024) { // Sanity check: max 1MB
        clear(); // Invalid message size, reset buffer
        return false;
    }
    
    if (available < 4 + length) {
        return false; // Not enough data yet
    }
    
    // Extract message without copying (use string_view-like approach)
    // But we need to return a string, so we'll copy efficiently
    message.assign(buffer_.data() + readPos_ + 4, length);
    readPos_ += 4 + length;
    
    // Compact if we've read a significant portion
    compactIfNeeded();
    
    return true;
}

void MessageBuffer::clear() {
    buffer_.clear();
    readPos_ = 0;
}

void MessageBuffer::compactIfNeeded() {
    // Compact if we've read more than half the buffer or buffer is > 1MB
    // This avoids unbounded growth while minimizing copies
    if (readPos_ > 0 && (readPos_ > buffer_.size() / 2 || buffer_.size() > 1024 * 1024)) {
        buffer_.erase(0, readPos_);
        readPos_ = 0;
    }
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
        
        int pollResult = poll(&pfd, 1, 1); // 1ms timeout for low latency
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
    
    int pollResult = poll(&pfd, 1, 1); // 1ms timeout for low latency
    if (pollResult <= 0) {
        return false; // Timeout or error
    }
    
    if (!(pfd.revents & POLLIN)) {
        return false; // No data available
    }
    
    // Increased buffer size to reduce syscalls for large messages
    char recvBuffer[8192]; // 8KB buffer
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
    // NOTE: This is a legacy wrapper. For proper per-connection buffering,
    // use receiveFramedMessage() directly with a per-connection MessageBuffer.
    // This function creates a temporary buffer which is inefficient.
    MessageBuffer buffer;
    if (!clientSocket || *clientSocket < 0) {
        return false;
    }
    return receiveFramedMessage(*clientSocket, buffer, message);
}

bool receiveFromServer(const SocketPtr& clientSocket, std::string& message) {
    // NOTE: This is a legacy wrapper. For proper per-connection buffering,
    // use receiveFramedMessage() directly with a per-connection MessageBuffer.
    // This function creates a temporary buffer which is inefficient.
    MessageBuffer buffer;
    if (!clientSocket || *clientSocket < 0) {
        return false;
    }
    return receiveFramedMessage(*clientSocket, buffer, message);
}
