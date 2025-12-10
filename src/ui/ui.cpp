#include "ui.h"
#include <iostream>
#include <sys/poll.h>
#include <unistd.h>

void displayMenu(SocketPtr serverSocket, 
                 const std::vector<ClientConnectionPtr>& serverClients,
                 const std::vector<ClientConnectionPtr>& clientConnections) {
    std::cout << "\n========================================\n";
    std::cout << "     HFT Gateway Control Menu\n";
    std::cout << "========================================\n";
    std::cout << "  Server: " << (serverSocket ? "Listening" : "Not running");
    if (serverSocket && !serverClients.empty()) {
        std::cout << " (" << serverClients.size() << " client(s) connected)";
    }
    std::cout << "\n";
    std::cout << "  Client: ";
    if (clientConnections.empty()) {
        std::cout << "Not connected";
    } else {
        // Count connected clients
        int connectedCount = 0;
        std::vector<int> connectedIds;
        for (const auto& conn : clientConnections) {
            if (conn->connected && conn->socket) {
                connectedCount++;
                connectedIds.push_back(conn->id);
            }
        }
        if (connectedCount == 0) {
            std::cout << "Not connected";
        } else {
            std::cout << connectedCount << " connected";
            if (connectedCount <= 5) {
                std::cout << " (IDs: ";
                for (size_t i = 0; i < connectedIds.size(); ++i) {
                    std::cout << connectedIds[i];
                    if (i < connectedIds.size() - 1) {
                        std::cout << ", ";
                    }
                }
                std::cout << ")";
            } else {
                std::cout << " (IDs: ";
                for (size_t i = 0; i < 5; ++i) {
                    std::cout << connectedIds[i];
                    if (i < 4) {
                        std::cout << ", ";
                    }
                }
                std::cout << ", ...)";
            }
        }
    }
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  1. Create server socket\n";
    std::cout << "  2. Connect to server\n";
    std::cout << "  3. Send message (server -> client)\n";
    std::cout << "  4. Send message (client -> server)\n";
    std::cout << "  5. Stop server connection\n";
    std::cout << "  6. Stop client connection\n";
    std::cout << "  7. View received messages\n";
    std::cout << "========================================\n";
    std::cout << "Enter your choice (1-7): ";
}

bool hasInput() {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}
