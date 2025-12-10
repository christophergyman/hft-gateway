#include "message.h"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>

MessageQueue receivedMessages;

bool MessageBuffer::addData(const char* data, size_t len) {
    compactIfNeeded();
    buffer_.append(data, len);
    return true;
}

bool MessageBuffer::extractMessage(std::string& message) {
    const size_t available = buffer_.size() - readPos_;
    
    if (available < 4) {
        return false; // Need 4 bytes for length header
    }
    
    // Read length field (network byte order)
    uint32_t length;
    std::memcpy(&length, buffer_.data() + readPos_, 4);
    length = ntohl(length);
    
    // Reject messages > 1MB to prevent memory exhaustion
    if (length > 1024 * 1024) {
        clear();
        return false;
    }
    
    if (available < 4 + length) {
        return false; // Incomplete message
    }
    
    message.assign(buffer_.data() + readPos_ + 4, length);
    readPos_ += 4 + length;
    compactIfNeeded();
    
    return true;
}

void MessageBuffer::clear() {
    buffer_.clear();
    readPos_ = 0;
}

void MessageBuffer::compactIfNeeded() {
    // Compact when readPos_ > half buffer size or buffer > 1MB
    if (readPos_ > 0 && (readPos_ > buffer_.size() / 2 || buffer_.size() > 1024 * 1024)) {
        buffer_.erase(0, readPos_);
        readPos_ = 0;
    }
}


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


bool sendFramedMessage(int socketFd, const std::string& message) {
    if (socketFd < 0 || message.empty()) {
        return false;
    }
    
    // Frame: [4 bytes: length (network byte order)][N bytes: payload]
    uint32_t length = htonl(static_cast<uint32_t>(message.size()));
    std::vector<char> framed(4 + message.size());
    std::memcpy(framed.data(), &length, 4);
    std::memcpy(framed.data() + 4, message.data(), message.size());
    
    // Handle partial writes (non-blocking sockets)
    size_t bytesSent = 0;
    const size_t totalLength = framed.size();
    
    while (bytesSent < totalLength) {
        struct pollfd pfd;
        pfd.fd = socketFd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        
        // 1ms timeout for low latency
        int pollResult = poll(&pfd, 1, 1);
        if (pollResult < 0) {
            return false;
        }
        if (pollResult == 0 || !(pfd.revents & POLLOUT)) {
            continue;
        }
        
        // MSG_NOSIGNAL prevents SIGPIPE on Linux (SO_NOSIGPIPE on macOS)
        #ifdef MSG_NOSIGNAL
        const ssize_t sent = send(socketFd, framed.data() + bytesSent, 
                                  totalLength - bytesSent, MSG_NOSIGNAL);
        #else
        const ssize_t sent = send(socketFd, framed.data() + bytesSent, 
                                  totalLength - bytesSent, 0);
        #endif
        
        if (sent <= 0) {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            return false;
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
    
    struct pollfd pfd;
    pfd.fd = socketFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    // 1ms timeout for low latency
    int pollResult = poll(&pfd, 1, 1);
    if (pollResult <= 0 || !(pfd.revents & POLLIN)) {
        return false;
    }
    
    // 8KB buffer reduces syscalls for large messages
    char recvBuffer[8192];
    ssize_t bytesReceived = recv(socketFd, recvBuffer, sizeof(recvBuffer), 0);
    
    if (bytesReceived <= 0) {
        if (bytesReceived == 0 || (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false; // Connection closed or would block
        }
        return false;
    }
    
    buffer.addData(recvBuffer, bytesReceived);
    return buffer.extractMessage(message);
}

bool receiveFromClient(const SocketPtr& clientSocket, std::string& message) {
    // Legacy wrapper - creates temporary buffer (inefficient for partial messages)
    // Use receiveFramedMessage() with per-connection MessageBuffer instead
    MessageBuffer buffer;
    if (!clientSocket || *clientSocket < 0) {
        return false;
    }
    return receiveFramedMessage(*clientSocket, buffer, message);
}

bool receiveFromServer(const SocketPtr& clientSocket, std::string& message) {
    // Legacy wrapper - creates temporary buffer (inefficient for partial messages)
    // Use receiveFramedMessage() with per-connection MessageBuffer instead
    MessageBuffer buffer;
    if (!clientSocket || *clientSocket < 0) {
        return false;
    }
    return receiveFramedMessage(*clientSocket, buffer, message);
}
