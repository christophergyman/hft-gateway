#include "ui.h"
#include <iostream>
#include <sys/poll.h>
#include <unistd.h>

void displayMenu(SocketPtr serverSocket, 
                 const std::vector<ClientConnectionPtr>& clients,
                 SocketPtr clientSocket, 
                 std::atomic<bool>& clientConnected) {
    std::cout << "\n========================================\n";
    std::cout << "     HFT Gateway Control Menu\n";
    std::cout << "========================================\n";
    std::cout << "  Server: " << (serverSocket ? "Listening" : "Not running");
    if (serverSocket && !clients.empty()) {
        std::cout << " (" << clients.size() << " client(s) connected)";
    }
    std::cout << "\n";
    std::cout << "  Client: " << (clientSocket && clientConnected ? "Connected" : "Not connected") << "\n";
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
