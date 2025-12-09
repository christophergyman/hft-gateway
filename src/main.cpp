#include "socket_utils.h"
#include "message.h"
#include "connection.h"
#include "server.h"
#include "client.h"
#include "ui.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <algorithm>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <chrono>

int main() {
    SocketPtr serverSocket = nullptr;
    
    std::thread serverAcceptThreadHandle;
    std::thread clientConnectThreadHandle;
    
    std::atomic<bool> serverAcceptRunning(false);
    std::atomic<bool> clientConnectRunning(false);
    std::atomic<bool> connectComplete(false);
    std::atomic<int> nextClientId(1);
    std::atomic<int> nextClientConnectionId(1);
    
    std::vector<ClientConnectionPtr> serverClients;
    std::mutex serverClientsMutex;
    
    std::vector<ClientConnectionPtr> clientConnections;
    std::mutex clientConnectionsMutex;
    
    SocketPtr pendingClientSocket = nullptr;
    bool pendingConnectSuccess = false;
    int pendingConnectionId = 0;
    
    std::string input;
    int choice;
    bool menuDisplayed = false;

    // Display menu initially
    displayMenu(serverSocket, serverClients, clientConnections);
    menuDisplayed = true;

    while (true) {
        // Check for received messages and display them (non-blocking)
        std::string source, message;
        while (receivedMessages.pop(source, message)) {
            std::cout << "\n[Received from " << source << "] " << message << std::endl;
        }
        
        // Clean up disconnected clients
        {
            std::lock_guard<std::mutex> lock(serverClientsMutex);
            serverClients.erase(
                std::remove_if(serverClients.begin(), serverClients.end(),
                    [](const ClientConnectionPtr& conn) {
                        return !conn->connected && !conn->running;
                    }),
                serverClients.end());
        }
        
        // Clean up disconnected client connections
        {
            std::lock_guard<std::mutex> lock(clientConnectionsMutex);
            clientConnections.erase(
                std::remove_if(clientConnections.begin(), clientConnections.end(),
                    [](const ClientConnectionPtr& conn) {
                        return !conn->connected && !conn->running;
                    }),
                clientConnections.end());
        }
        
        // Non-blocking menu display and input
        if (hasInput()) {
            if (!menuDisplayed) {
                displayMenu(serverSocket, serverClients, clientConnections);
                menuDisplayed = true;
            }
            std::getline(std::cin, input);
            
            if (input.empty()) {
                menuDisplayed = false;
                continue;
            }
            
            menuDisplayed = false;
            choice = input[0] - '0';
            
            switch (choice) {
                case 1: {
                    if (serverSocket) {
                        std::cout << "\n[Error] Server already running. Stop it first (option 5).\n";
                        break;
                    }
                    std::cout << "\n[Action] Creating server socket and waiting for clients...\n";
                    
                    // Create server socket
                    serverSocket = startServer();
                    if (serverSocket) {
                        // Start accept thread for multiple clients
                        serverAcceptRunning = true;
                        nextClientId = 1;
                        serverAcceptThreadHandle = std::thread(serverAcceptThread,
                                                               serverSocket,
                                                               std::ref(serverAcceptRunning),
                                                               std::ref(serverClients),
                                                               std::ref(serverClientsMutex),
                                                               std::ref(nextClientId),
                                                               1000); // Max 1000 connections
                        std::cout << "[Success] Server socket created! Waiting for client connections...\n";
                    } else {
                        std::cout << "[Error] Failed to create server socket.\n";
                    }
                    break;
                }
                
                case 2: {
                    std::cout << "\n[Action] Connecting to server (127.0.0.1:8080)...\n";
                    
                    int clientSocketFd = socket(AF_INET, SOCK_STREAM, 0);
                    if (clientSocketFd < 0) {
                        std::cout << "[Error] Failed to create client socket.\n";
                        break;
                    }
                    
                    int connectionId = nextClientConnectionId++;
                    auto tempSocket = SocketPtr(new int(clientSocketFd), [](int* s){
                        if (s && *s >= 0) {
                            close(*s);
                        }
                        delete s;
                    });
                    
                    // Connect in background thread
                    clientConnectRunning = true;
                    connectComplete = false;
                    pendingClientSocket = tempSocket;
                    pendingConnectSuccess = false;
                    pendingConnectionId = connectionId;
                    clientConnectThreadHandle = std::thread(clientConnectThread,
                                                           tempSocket,
                                                           std::ref(clientConnectRunning),
                                                           std::ref(connectComplete),
                                                           std::ref(pendingConnectSuccess),
                                                           "127.0.0.1", 5);
                    
                    std::cout << "[Info] Connection attempt " << connectionId << " in progress...\n";
                    break;
                }
                
                case 3: {
                    std::lock_guard<std::mutex> lock(serverClientsMutex);
                    if (serverClients.empty()) {
                        std::cout << "\n[Error] No clients connected. Please wait for a client to connect.\n";
                        break;
                    }
                    
                    std::cout << "\n[Action] Enter message to send from server to client: ";
                    std::string message;
                    std::getline(std::cin, message);
                    if (message.empty()) {
                        std::cout << "[Error] Message cannot be empty.\n";
                        break;
                    }
                    
                    // Send to all connected clients
                    bool anySent = false;
                    for (auto& client : serverClients) {
                        if (client->connected && client->socket) {
                            auto msgPtr = std::make_shared<std::string>(message);
                            if (sendToClient(client->socket, msgPtr)) {
                                anySent = true;
                            }
                        }
                    }
                    
                    if (anySent) {
                        std::cout << "[Success] Message sent to " << serverClients.size() << " client(s)!\n";
                    } else {
                        std::cout << "[Error] Failed to send message to any client.\n";
                    }
                    break;
                }
                
                case 4: {
                    // Get list of connected clients (with lock)
                    std::vector<ClientConnectionPtr> connectedClients;
                    {
                        std::lock_guard<std::mutex> lock(clientConnectionsMutex);
                        if (clientConnections.empty()) {
                            std::cout << "\n[Error] No client connections. Please connect to server first (option 2).\n";
                            break;
                        }
                        
                        // Filter to only connected clients
                        for (auto& conn : clientConnections) {
                            if (conn->connected && conn->socket) {
                                connectedClients.push_back(conn);
                            }
                        }
                    }
                    
                    if (connectedClients.empty()) {
                        std::cout << "\n[Error] No active client connections. Please connect to server first (option 2).\n";
                        break;
                    }
                    
                    // Display available clients (no lock needed for read-only)
                    std::cout << "\n[Action] Available client connections:\n";
                    for (size_t i = 0; i < connectedClients.size(); ++i) {
                        std::cout << "  " << (i + 1) << ". Client ID " << connectedClients[i]->id << "\n";
                    }
                    std::cout << "Select client (1-" << connectedClients.size() << "): ";
                    std::string clientChoiceStr;
                    std::getline(std::cin, clientChoiceStr);
                    
                    int clientChoice = 0;
                    try {
                        clientChoice = std::stoi(clientChoiceStr);
                    } catch (...) {
                        std::cout << "[Error] Invalid client selection.\n";
                        break;
                    }
                    
                    if (clientChoice < 1 || clientChoice > static_cast<int>(connectedClients.size())) {
                        std::cout << "[Error] Invalid client selection.\n";
                        break;
                    }
                    
                    auto selectedClient = connectedClients[clientChoice - 1];
                    
                    std::cout << "\n[Action] Enter message to send from client " << selectedClient->id << " to server: ";
                    std::string message;
                    std::getline(std::cin, message);
                    if (message.empty()) {
                        std::cout << "[Error] Message cannot be empty.\n";
                        break;
                    }
                    auto msgPtr = std::make_shared<std::string>(message);
                    if (sendToServer(selectedClient->socket, msgPtr)) {
                        std::cout << "[Success] Message sent successfully from client " << selectedClient->id << "!\n";
                    } else {
                        std::cout << "[Error] Failed to send message from client " << selectedClient->id << ".\n";
                        selectedClient->connected = false;
                    }
                    break;
                }
                
                case 5: {
                    if (!serverSocket) {
                        std::cout << "\n[Error] No server connection to stop.\n";
                        break;
                    }
                    
                    // Stop accept thread first by closing socket
                    serverAcceptRunning = false;
                    if (serverSocket) {
                        close(*serverSocket); // Close socket to break accept
                    }
                    
                    // Stop all client receive threads
                    {
                        std::lock_guard<std::mutex> lock(serverClientsMutex);
                        for (auto& client : serverClients) {
                            client->running = false;
                            client->connected = false;
                        }
                    }
                    
                    // Join accept thread (socket already closed)
                    if (serverAcceptThreadHandle.joinable()) {
                        serverAcceptThreadHandle.join();
                    }
                    
                    // Join all client receive threads
                    {
                        std::lock_guard<std::mutex> lock(serverClientsMutex);
                        for (auto& client : serverClients) {
                            if (client->receiveThread.joinable()) {
                                client->receiveThread.join();
                            }
                        }
                        serverClients.clear();
                    }
                    
                    serverSocket.reset();
                    std::cout << "\n[Success] Server connection stopped.\n";
                    break;
                }
                
                case 6: {
                    // Get list of connected clients (with lock)
                    std::vector<ClientConnectionPtr> connectedClients;
                    {
                        std::lock_guard<std::mutex> lock(clientConnectionsMutex);
                        if (clientConnections.empty()) {
                            std::cout << "\n[Error] No client connections to stop.\n";
                            break;
                        }
                        
                        // Filter to only connected clients
                        for (auto& conn : clientConnections) {
                            if (conn->connected && conn->socket) {
                                connectedClients.push_back(conn);
                            }
                        }
                    }
                    
                    if (connectedClients.empty()) {
                        std::cout << "\n[Error] No active client connections to stop.\n";
                        break;
                    }
                    
                    // Display available clients (no lock needed for read-only)
                    std::cout << "\n[Action] Available client connections:\n";
                    for (size_t i = 0; i < connectedClients.size(); ++i) {
                        std::cout << "  " << (i + 1) << ". Client ID " << connectedClients[i]->id << "\n";
                    }
                    std::cout << "  " << (connectedClients.size() + 1) << ". Disconnect all\n";
                    std::cout << "Select client to disconnect (1-" << (connectedClients.size() + 1) << "): ";
                    std::string clientChoiceStr;
                    std::getline(std::cin, clientChoiceStr);
                    
                    int clientChoice = 0;
                    try {
                        clientChoice = std::stoi(clientChoiceStr);
                    } catch (...) {
                        std::cout << "[Error] Invalid client selection.\n";
                        break;
                    }
                    
                    if (clientChoice < 1 || clientChoice > static_cast<int>(connectedClients.size() + 1)) {
                        std::cout << "[Error] Invalid client selection.\n";
                        break;
                    }
                    
                    std::vector<ClientConnectionPtr> toDisconnect;
                    if (clientChoice == static_cast<int>(connectedClients.size() + 1)) {
                        // Disconnect all
                        toDisconnect = connectedClients;
                    } else {
                        // Disconnect specific client
                        toDisconnect.push_back(connectedClients[clientChoice - 1]);
                    }
                    
                    // Stop connections (no lock needed - we're modifying shared_ptr objects)
                    for (auto& conn : toDisconnect) {
                        conn->running = false;
                        conn->connected = false;
                        if (conn->socket) {
                            close(*conn->socket);
                        }
                    }
                    
                    // Remove from vector (with lock)
                    {
                        std::lock_guard<std::mutex> lock(clientConnectionsMutex);
                        if (clientChoice == static_cast<int>(connectedClients.size() + 1)) {
                            clientConnections.clear();
                        } else {
                            int selectedId = connectedClients[clientChoice - 1]->id;
                            clientConnections.erase(
                                std::remove_if(clientConnections.begin(), clientConnections.end(),
                                    [selectedId](const ClientConnectionPtr& conn) {
                                        return conn->id == selectedId;
                                    }),
                                clientConnections.end());
                        }
                    }
                    
                    // Join threads (no lock needed)
                    for (auto& conn : toDisconnect) {
                        if (conn->receiveThread.joinable()) {
                            conn->receiveThread.join();
                        }
                    }
                    
                    if (clientChoice == static_cast<int>(connectedClients.size() + 1)) {
                        std::cout << "\n[Success] All client connections stopped.\n";
                    } else {
                        std::cout << "\n[Success] Client connection " << connectedClients[clientChoice - 1]->id << " stopped.\n";
                    }
                    
                    // Stop connect thread if running
                    clientConnectRunning = false;
                    if (clientConnectThreadHandle.joinable()) {
                        clientConnectThreadHandle.join();
                    }
                    break;
                }
                
                case 7: {
                    std::cout << "\n[Received Messages]\n";
                    std::cout << "========================================\n";
                    bool hasMessages = false;
                    std::string source, msg;
                    while (receivedMessages.pop(source, msg)) {
                        std::cout << "[" << source << "] " << msg << "\n";
                        hasMessages = true;
                    }
                    if (!hasMessages) {
                        std::cout << "No messages received yet.\n";
                    }
                    std::cout << "========================================\n";
                    break;
                }
                
                default:
                    std::cout << "\n[Error] Invalid choice. Please enter a number between 1-7.\n";
                    break;
            }
        } else {
            // No input available, minimal sleep to reduce CPU usage while maintaining low latency
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        // Check if connect completed
        if (connectComplete && pendingClientSocket) {
            if (clientConnectThreadHandle.joinable()) {
                clientConnectThreadHandle.join();
            }
            
            // Get connection result from thread
            int error = 0;
            socklen_t len = sizeof(error);
            bool success = false;
            if (getsockopt(*pendingClientSocket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                success = true;
            }
            
            if (success && pendingConnectSuccess) {
                // Connection successful - create ClientConnection
                int connectionId = pendingConnectionId;
                auto clientConn = std::make_shared<ClientConnection>(connectionId);
                clientConn->socket = pendingClientSocket;
                clientConn->running = true;
                clientConn->connected = true;
                
                // Start receive thread
                clientConn->receiveThread = std::thread(clientReceiveThread,
                                                        clientConn->socket,
                                                        std::ref(clientConn->running),
                                                        std::ref(clientConn->connected),
                                                        std::ref(clientConn->buffer));
                
                // Add to client connections list
                {
                    std::lock_guard<std::mutex> lock(clientConnectionsMutex);
                    clientConnections.push_back(clientConn);
                }
                
                std::cout << "\n[Success] Client connection " << connectionId << " connected to server!\n";
            } else {
                std::cout << "\n[Error] Failed to connect to server.\n";
            }
            
            pendingClientSocket.reset();
            pendingConnectionId = 0;
            connectComplete = false;
        }
    }
    
    // Cleanup on exit - close sockets before joining threads
    serverAcceptRunning = false;
    clientConnectRunning = false;
    
    // Close sockets first to break blocking calls
    if (serverSocket) {
        close(*serverSocket);
    }
    if (pendingClientSocket) {
        close(*pendingClientSocket);
    }
    
    // Stop all server client connections
    {
        std::lock_guard<std::mutex> lock(serverClientsMutex);
        for (auto& client : serverClients) {
            client->running = false;
            client->connected = false;
            if (client->socket) {
                close(*client->socket);
            }
        }
    }
    
    // Stop all client connections
    {
        std::lock_guard<std::mutex> lock(clientConnectionsMutex);
        for (auto& conn : clientConnections) {
            conn->running = false;
            conn->connected = false;
            if (conn->socket) {
                close(*conn->socket);
            }
        }
    }
    
    // Join threads
    if (serverAcceptThreadHandle.joinable()) {
        serverAcceptThreadHandle.join();
    }
    if (clientConnectThreadHandle.joinable()) {
        clientConnectThreadHandle.join();
    }
    
    // Join server client receive threads
    {
        std::lock_guard<std::mutex> lock(serverClientsMutex);
        for (auto& client : serverClients) {
            if (client->receiveThread.joinable()) {
                client->receiveThread.join();
            }
        }
    }
    
    // Join client connection receive threads
    {
        std::lock_guard<std::mutex> lock(clientConnectionsMutex);
        for (auto& conn : clientConnections) {
            if (conn->receiveThread.joinable()) {
                conn->receiveThread.join();
            }
        }
    }
    
    return 0;
}
