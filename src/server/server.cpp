#include "server.h"
#include "../network/socket_utils.h"
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/tcp.h>
#include <cerrno>
#include <unistd.h>
#include <algorithm>

void serverReceiveThread(ClientConnectionPtr clientConn) {
    if (!clientConn || !clientConn->socket || *clientConn->socket < 0) {
        return;
    }
    
    clientConn->connected = true;
    std::string message;
    
    while (clientConn->running && clientConn->connected && 
           clientConn->socket && *clientConn->socket >= 0) {
        if (receiveFramedMessage(*clientConn->socket, clientConn->buffer, message)) {
            std::string formattedMsg = "[SERVER] receives [CLIENT" + 
                                       std::to_string(clientConn->id) + 
                                       "] message [\"" + message + "\"]";
            receivedMessages.push("Server", formattedMsg);
        } else {
            // Check if connection was closed
            struct pollfd pfd;
            pfd.fd = *clientConn->socket;
            pfd.events = POLLIN;
            pfd.revents = 0;
            
            if (poll(&pfd, 1, 0) < 0 || (pfd.revents & (POLLERR | POLLHUP))) {
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
                        std::atomic<int>& nextClientId,
                        size_t maxConnections) {
    if (!serverSocket || *serverSocket < 0) {
        return;
    }
    
    while (running && serverSocket && *serverSocket >= 0) {
        struct pollfd pfd;
        pfd.fd = *serverSocket;
        pfd.events = POLLIN;
        pfd.revents = 0;
        
        // 1ms timeout for low latency
        int pollResult = poll(&pfd, 1, 1);
        
        if (pollResult < 0) {
            break;
        }
        if (pollResult == 0 || !(pfd.revents & POLLIN)) {
            continue;
        }
        
        // Check connection limit and clean up disconnected clients
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients.erase(
                std::remove_if(clients.begin(), clients.end(),
                    [](const ClientConnectionPtr& conn) {
                        return !conn->connected && !conn->running;
                    }),
                clients.end());
            
            if (clients.size() >= maxConnections) {
                // Accept and immediately close to prevent queue buildup
                int tempFd = accept(*serverSocket, nullptr, nullptr);
                if (tempFd >= 0) {
                    close(tempFd);
                    receivedMessages.push("System", "Connection rejected: maximum connections reached");
                }
                continue;
            }
        }
        
        int clientSocketFd = accept(*serverSocket, nullptr, nullptr);
        if (clientSocketFd >= 0 && running) {
            makeNonBlocking(clientSocketFd);
            
            int opt = 1;
            #ifdef SO_NOSIGPIPE
            setsockopt(clientSocketFd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
            #endif
            
            // TCP_NODELAY for low latency, 64KB buffers for throughput
            opt = 1;
            setsockopt(clientSocketFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
            int bufferSize = 64 * 1024;
            setsockopt(clientSocketFd, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));
            setsockopt(clientSocketFd, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));
            
            int clientId = nextClientId++;
            auto clientConn = std::make_shared<ClientConnection>(clientId);
            clientConn->socket = SocketPtr(new int(clientSocketFd), [](int* s){
                if (s && *s >= 0) {
                    close(*s);
                }
                delete s;
            });
            clientConn->running = true;
            clientConn->receiveThread = std::thread(serverReceiveThread, clientConn);
            
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.push_back(clientConn);
            }
            
            receivedMessages.push("System", "Client " + std::to_string(clientId) + " connected");
        } else if (clientSocketFd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }
}
