#include "client.h"
#include "socket_utils.h"
#include "message.h"
#include <sys/socket.h>
#include <sys/poll.h>
#include <cerrno>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <chrono>
#include <algorithm>

void clientReceiveThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running, 
                        std::atomic<bool>& connected, 
                        MessageBuffer& buffer) {
    if (!clientSocket || *clientSocket < 0) {
        connected = false;
        return;
    }
    
    connected = true;
    std::string message;
    
    while (running && connected && clientSocket && *clientSocket >= 0) {
        if (receiveFramedMessage(*clientSocket, buffer, message)) {
            receivedMessages.push("Server", message);
        } else {
            // Check if connection was closed
            struct pollfd pfd;
            pfd.fd = *clientSocket;
            pfd.events = POLLIN;
            pfd.revents = 0;
            
            if (poll(&pfd, 1, 0) < 0 || (pfd.revents & (POLLERR | POLLHUP))) {
                // Connection error or closed
                connected = false;
                receivedMessages.push("System", "Server disconnected");
                break;
            }
        }
    }
    
    connected = false;
}

void clientConnectThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running,
                        std::atomic<bool>& connectComplete, 
                        bool& connectSuccess,
                        const std::string& serverAddr, 
                        int timeoutSeconds) {
    if (!clientSocket || *clientSocket < 0) {
        connectSuccess = false;
        connectComplete = true;
        return;
    }
    
    // Make socket non-blocking
    if (!makeNonBlocking(*clientSocket)) {
        connectSuccess = false;
        connectComplete = true;
        return;
    }
    
    // Prevent SIGPIPE
    int opt = 1;
    #ifdef SO_NOSIGPIPE
    setsockopt(*clientSocket, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    #endif
    
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    
    // Parse server address
    if (inet_pton(AF_INET, serverAddr.c_str(), &serverAddress.sin_addr) <= 0) {
        // Fallback to loopback if parsing fails
        serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    
    // Initiate non-blocking connect
    int result = connect(*clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    
    if (result == 0) {
        // Connected immediately
        connectSuccess = true;
        connectComplete = true;
        return;
    }
    
    if (errno != EINPROGRESS) {
        connectSuccess = false;
        connectComplete = true;
        return;
    }
    
    // Wait for connection with timeout using poll
    struct pollfd pfd;
    pfd.fd = *clientSocket;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    
    auto startTime = std::chrono::steady_clock::now();
    int timeoutMs = timeoutSeconds * 1000;
    
    while (running) {
        int remainingMs = timeoutMs - std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        
        if (remainingMs <= 0) {
            connectSuccess = false;
            connectComplete = true;
            return;
        }
        
        int pollResult = poll(&pfd, 1, std::min(100, remainingMs));
        
        if (pollResult < 0) {
            connectSuccess = false;
            connectComplete = true;
            return;
        }
        
        if (pollResult > 0 && (pfd.revents & POLLOUT)) {
            // Check if connection succeeded
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(*clientSocket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                connectSuccess = true;
            } else {
                connectSuccess = false;
            }
            connectComplete = true;
            return;
        }
    }
    
    connectSuccess = false;
    connectComplete = true;
}
