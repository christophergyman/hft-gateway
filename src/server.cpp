#include "server.h"
#include "socket_utils.h"
#include <sys/socket.h>
#include <sys/poll.h>
#include <cerrno>
#include <unistd.h>
#include <iostream>

void serverReceiveThread(ClientConnectionPtr clientConn) {
    if (!clientConn || !clientConn->socket || *clientConn->socket < 0) {
        return;
    }
    
    clientConn->connected = true;
    std::string message;
    
    while (clientConn->running && clientConn->connected && 
           clientConn->socket && *clientConn->socket >= 0) {
        if (receiveFramedMessage(*clientConn->socket, clientConn->buffer, message)) {
            receivedMessages.push("Client", message);
        } else {
            // Check if connection was closed
            struct pollfd pfd;
            pfd.fd = *clientConn->socket;
            pfd.events = POLLIN;
            pfd.revents = 0;
            
            if (poll(&pfd, 1, 0) < 0 || (pfd.revents & (POLLERR | POLLHUP))) {
                // Connection error or closed
                clientConn->connected = false;
                receivedMessages.push("System", "Client disconnected");
                break;
            }
        }
    }
    
    clientConn->connected = false;
}

void serverAcceptThread(SocketPtr serverSocket, 
                        std::atomic<bool>& running, 
                        std::vector<ClientConnectionPtr>& clients,
                        std::mutex& clientsMutex, 
                        std::atomic<int>& nextClientId) {
    if (!serverSocket || *serverSocket < 0) {
        return;
    }
    
    // Accept loop for multiple clients
    while (running && serverSocket && *serverSocket >= 0) {
        struct pollfd pfd;
        pfd.fd = *serverSocket;
        pfd.events = POLLIN;
        pfd.revents = 0;
        
        // Poll with 100ms timeout to check running flag
        int pollResult = poll(&pfd, 1, 100);
        
        if (pollResult < 0) {
            break; // Error
        }
        
        if (pollResult == 0) {
            continue; // Timeout, check running flag
        }
        
        if (!(pfd.revents & POLLIN)) {
            continue;
        }
        
        // Accept new client
        int clientSocketFd = accept(*serverSocket, nullptr, nullptr);
        if (clientSocketFd >= 0 && running) {
            // Make client socket non-blocking
            makeNonBlocking(clientSocketFd);
            
            // Prevent SIGPIPE
            int opt = 1;
            #ifdef SO_NOSIGPIPE
            setsockopt(clientSocketFd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
            #endif
            
            // Create client connection
            int clientId = nextClientId++;
            auto clientConn = std::make_shared<ClientConnection>(clientId);
            clientConn->socket = SocketPtr(new int(clientSocketFd), [](int* s){
                if (s && *s >= 0) {
                    close(*s);
                }
                delete s;
            });
            clientConn->running = true;
            
            // Start receive thread
            clientConn->receiveThread = std::thread(serverReceiveThread, clientConn);
            
            // Add to clients list
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.push_back(clientConn);
            }
            
            receivedMessages.push("System", "Client " + std::to_string(clientId) + " connected");
        } else if (clientSocketFd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break; // Error accepting
        }
    }
}
