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
    SocketPtr clientSocket = nullptr;
    
    std::thread serverAcceptThreadHandle;
    std::thread clientReceiveThreadHandle;
    std::thread clientConnectThreadHandle;
    
    std::atomic<bool> serverAcceptRunning(false);
    std::atomic<bool> clientReceiveRunning(false);
    std::atomic<bool> clientConnectRunning(false);
    std::atomic<bool> clientConnected(false);
    std::atomic<bool> connectComplete(false);
    std::atomic<int> nextClientId(1);
    
    std::vector<ClientConnectionPtr> serverClients;
    std::mutex serverClientsMutex;
    
    MessageBuffer clientBuffer;
    
    std::string input;
    int choice;

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
        
        // Non-blocking menu display and input
        if (hasInput()) {
            displayMenu(serverSocket, serverClients, clientSocket, clientConnected);
            std::getline(std::cin, input);
            
            if (input.empty()) {
                continue;
            }
            
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
                                                               std::ref(nextClientId));
                        std::cout << "[Success] Server socket created! Waiting for client connections...\n";
                    } else {
                        std::cout << "[Error] Failed to create server socket.\n";
                    }
                    break;
                }
                
                case 2: {
                    if (clientSocket && clientConnected) {
                        std::cout << "\n[Error] Client already connected. Disconnect first (option 6).\n";
                        break;
                    }
                    std::cout << "\n[Action] Connecting to server (127.0.0.1:8080)...\n";
                    
                    int clientSocketFd = socket(AF_INET, SOCK_STREAM, 0);
                    if (clientSocketFd < 0) {
                        std::cout << "[Error] Failed to create client socket.\n";
                        break;
                    }
                    
                    clientSocket = SocketPtr(new int(clientSocketFd), [](int* s){
                        if (s && *s >= 0) {
                            close(*s);
                        }
                        delete s;
                    });
                    
                    // Connect in background thread
                    clientConnectRunning = true;
                    connectComplete = false;
                    bool localConnectSuccess = false;
                    clientConnectThreadHandle = std::thread(clientConnectThread,
                                                           clientSocket,
                                                           std::ref(clientConnectRunning),
                                                           std::ref(connectComplete),
                                                           std::ref(localConnectSuccess),
                                                           "127.0.0.1", 5);
                    
                    std::cout << "[Info] Connection attempt in progress...\n";
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
                    if (!clientSocket || !clientConnected) {
                        std::cout << "\n[Error] Client socket not connected. Please connect to server first (option 2).\n";
                        break;
                    }
                    std::cout << "\n[Action] Enter message to send from client to server: ";
                    std::string message;
                    std::getline(std::cin, message);
                    if (message.empty()) {
                        std::cout << "[Error] Message cannot be empty.\n";
                        break;
                    }
                    auto msgPtr = std::make_shared<std::string>(message);
                    if (sendToServer(clientSocket, msgPtr)) {
                        std::cout << "[Success] Message sent successfully!\n";
                    } else {
                        std::cout << "[Error] Failed to send message.\n";
                        clientConnected = false;
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
                    if (!clientSocket) {
                        std::cout << "\n[Error] No client connection to stop.\n";
                        break;
                    }
                    
                    // Stop receive thread
                    clientReceiveRunning = false;
                    clientConnected = false;
                    
                    // Close socket before joining to break blocking calls
                    if (clientSocket) {
                        close(*clientSocket);
                    }
                    
                    if (clientReceiveThreadHandle.joinable()) {
                        clientReceiveThreadHandle.join();
                    }
                    
                    // Stop connect thread if running
                    clientConnectRunning = false;
                    if (clientConnectThreadHandle.joinable()) {
                        clientConnectThreadHandle.join();
                    }
                    
                    clientSocket.reset();
                    clientBuffer.clear();
                    std::cout << "\n[Success] Client connection stopped.\n";
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
            // No input available, just check for messages and wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Check if connect completed
        if (connectComplete && clientSocket && !clientReceiveRunning) {
            if (clientConnectThreadHandle.joinable()) {
                clientConnectThreadHandle.join();
            }
            
            // Get connection result from thread
            // Note: We need to check the socket state since the bool reference
            // might not be accessible. Check socket error status.
            int error = 0;
            socklen_t len = sizeof(error);
            bool success = false;
            if (getsockopt(*clientSocket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                success = true;
            }
            
            if (success) {
                // Connection successful
                clientReceiveRunning = true;
                clientConnected = true;
                clientBuffer.clear();
                clientReceiveThreadHandle = std::thread(clientReceiveThread,
                                                        clientSocket,
                                                        std::ref(clientReceiveRunning),
                                                        std::ref(clientConnected),
                                                        std::ref(clientBuffer));
                std::cout << "\n[Success] Connected to server!\n";
            } else {
                std::cout << "\n[Error] Failed to connect to server.\n";
                clientSocket.reset();
            }
            connectComplete = false;
        }
    }
    
    // Cleanup on exit - close sockets before joining threads
    serverAcceptRunning = false;
    clientReceiveRunning = false;
    clientConnectRunning = false;
    clientConnected = false;
    
    // Close sockets first to break blocking calls
    if (serverSocket) {
        close(*serverSocket);
    }
    if (clientSocket) {
        close(*clientSocket);
    }
    
    // Stop all client connections
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
    
    // Join threads
    if (serverAcceptThreadHandle.joinable()) {
        serverAcceptThreadHandle.join();
    }
    if (clientReceiveThreadHandle.joinable()) {
        clientReceiveThreadHandle.join();
    }
    if (clientConnectThreadHandle.joinable()) {
        clientConnectThreadHandle.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(serverClientsMutex);
        for (auto& client : serverClients) {
            if (client->receiveThread.joinable()) {
                client->receiveThread.join();
            }
        }
    }
    
    return 0;
}
