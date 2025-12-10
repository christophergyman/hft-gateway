/**
 * @file main.cpp
 * @brief Application entry point and main control loop
 * 
 * Architecture:
 * - Main thread: Menu loop, message display, connection management
 * - Server accept thread: Accepts new client connections
 * - Server receive threads: One per client, receives messages
 * - Client connect thread: Handles non-blocking connection attempts
 * - Client receive threads: One per connection, receives messages
 */

#include "network/socket_utils.h"
#include "network/message.h"
#include "network/connection.h"
#include "server/server.h"
#include "client/client.h"
#include "ui/ui.h"
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
    // ========================================================================
    // Server State
    // ========================================================================
    SocketPtr serverSocket = nullptr;  ///< Server listening socket
    
    std::thread serverAcceptThreadHandle;  ///< Thread handle for accept thread
    std::thread clientConnectThreadHandle;  ///< Thread handle for connect thread
    
    std::atomic<bool> serverAcceptRunning(false);  ///< Control flag for accept thread
    std::atomic<bool> clientConnectRunning(false);  ///< Control flag for connect thread
    std::atomic<bool> connectComplete(false);       ///< Flag indicating connect attempt finished
    std::atomic<int> nextClientId(1);                ///< Counter for server client IDs
    std::atomic<int> nextClientConnectionId(1);       ///< Counter for client connection IDs
    
    std::vector<ClientConnectionPtr> serverClients;  ///< Connected server clients
    std::mutex serverClientsMutex;                   ///< Mutex for serverClients vector
    
    // ========================================================================
    // Client State
    // ========================================================================
    std::vector<ClientConnectionPtr> clientConnections;  ///< Active client connections
    std::mutex clientConnectionsMutex;                   ///< Mutex for clientConnections vector
    
    SocketPtr pendingClientSocket = nullptr;  ///< Socket for pending connection attempt
    bool pendingConnectSuccess = false;       ///< Result of pending connection
    int pendingConnectionId = 0;              ///< ID for pending connection
    
    // ========================================================================
    // UI State
    // ========================================================================
    std::string input;        ///< User input string
    int choice;               ///< Parsed menu choice
    bool menuDisplayed = false;  ///< Flag to track if menu was displayed
    
    // ========================================================================
    // Message History
    // ========================================================================
    std::vector<std::string> messageHistory;  ///< History of received messages
    std::mutex messageHistoryMutex;           ///< Mutex for messageHistory
    const size_t MAX_HISTORY_SIZE = 1000;     ///< Maximum history entries

    // Display initial menu
    displayMenu(serverSocket, serverClients, clientConnections);
    menuDisplayed = true;

    // ========================================================================
    // Main Event Loop
    // ========================================================================
    while (true) {
        // Check for received messages and display them (non-blocking)
        // Messages are pushed by receive threads and consumed here
        std::string source, message;
        while (receivedMessages.pop(source, message)) {
            std::cout << "\n" << message << std::endl;
            
            // Store message in history for later viewing
            {
                std::lock_guard<std::mutex> lock(messageHistoryMutex);
                messageHistory.push_back(message);
                
                // Limit history size to prevent unbounded growth
                if (messageHistory.size() > MAX_HISTORY_SIZE) {
                    messageHistory.erase(messageHistory.begin());
                }
            }
        }
        
        // Clean up disconnected server clients
        // Remove clients that are both disconnected and stopped
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
        // Remove connections that are both disconnected and stopped
        {
            std::lock_guard<std::mutex> lock(clientConnectionsMutex);
            clientConnections.erase(
                std::remove_if(clientConnections.begin(), clientConnections.end(),
                    [](const ClientConnectionPtr& conn) {
                        return !conn->connected && !conn->running;
                    }),
                clientConnections.end());
        }
        
        // Non-blocking menu display and input handling
        if (hasInput()) {
            // Display menu if not already displayed
            if (!menuDisplayed) {
                displayMenu(serverSocket, serverClients, clientConnections);
                menuDisplayed = true;
            }
            
            // Read user input
            std::getline(std::cin, input);
            
            // Handle empty input (just Enter key)
            if (input.empty()) {
                menuDisplayed = false;
                continue;
            }
            
            menuDisplayed = false;
            choice = input[0] - '0';  // Parse first character as menu choice
            
            // Process menu selection
            switch (choice) {
                case 1: {
                    // Option 1: Create server socket
                    if (serverSocket) {
                        std::cout << "\n[Error] Server already running. Stop it first (option 5).\n";
                        break;
                    }
                    std::cout << "\n[Action] Creating server socket and waiting for clients...\n";
                    
                    // Create and configure server socket (listening on port 8080)
                    serverSocket = startServer();
                    if (serverSocket) {
                        // Start accept thread for handling multiple client connections
                        serverAcceptRunning = true;
                        nextClientId = 1;  // Reset client ID counter
                        serverAcceptThreadHandle = std::thread(serverAcceptThread,
                                                               serverSocket,
                                                               std::ref(serverAcceptRunning),
                                                               std::ref(serverClients),
                                                               std::ref(serverClientsMutex),
                                                               std::ref(nextClientId),
                                                               1000); // Maximum 1000 concurrent connections
                        std::cout << "[Success] Server socket created! Waiting for client connections...\n";
                    } else {
                        std::cout << "[Error] Failed to create server socket.\n";
                    }
                    break;
                }
                
                case 2: {
                    // Option 2: Connect to server
                    std::cout << "\n[Action] Connecting to server (127.0.0.1:8080)...\n";
                    
                    // Create client socket
                    int clientSocketFd = socket(AF_INET, SOCK_STREAM, 0);
                    if (clientSocketFd < 0) {
                        std::cout << "[Error] Failed to create client socket.\n";
                        break;
                    }
                    
                    // Generate unique connection ID
                    int connectionId = nextClientConnectionId++;
                    
                    // Wrap socket in smart pointer with custom deleter
                    auto tempSocket = SocketPtr(new int(clientSocketFd), [](int* s){
                        if (s && *s >= 0) {
                            close(*s);
                        }
                        delete s;
                    });
                    
                    // Start non-blocking connect in background thread
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
                                                           "127.0.0.1", 5);  // 5 second timeout
                    
                    std::cout << "[Info] Connection attempt " << connectionId << " in progress...\n";
                    break;
                }
                
                case 3: {
                    // Option 3: Send message from server to client(s)
                    std::lock_guard<std::mutex> lock(serverClientsMutex);
                    if (serverClients.empty()) {
                        std::cout << "\n[Error] No clients connected. Please wait for a client to connect.\n";
                        break;
                    }
                    
                    // Prompt for message
                    std::cout << "\n[Action] Enter message to send from server to client: ";
                    std::string message;
                    std::getline(std::cin, message);
                    if (message.empty()) {
                        std::cout << "[Error] Message cannot be empty.\n";
                        break;
                    }
                    
                    // Send message to all connected clients
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
                    // Option 5: Stop server connection
                    if (!serverSocket) {
                        std::cout << "\n[Error] No server connection to stop.\n";
                        break;
                    }
                    
                    // Stop accept thread by setting flag and closing socket
                    serverAcceptRunning = false;
                    if (serverSocket) {
                        close(*serverSocket); // Close socket to break accept() call
                    }
                    
                    // Stop all client receive threads
                    {
                        std::lock_guard<std::mutex> lock(serverClientsMutex);
                        for (auto& client : serverClients) {
                            client->running = false;
                            client->connected = false;
                        }
                    }
                    
                    // Wait for accept thread to finish
                    if (serverAcceptThreadHandle.joinable()) {
                        serverAcceptThreadHandle.join();
                    }
                    
                    // Wait for all client receive threads to finish
                    {
                        std::lock_guard<std::mutex> lock(serverClientsMutex);
                        for (auto& client : serverClients) {
                            if (client->receiveThread.joinable()) {
                                client->receiveThread.join();
                            }
                        }
                        serverClients.clear();  // Clear clients vector
                    }
                    
                    // Release server socket
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
                    // Option 7: View received messages history
                    std::cout << "\n[Received Messages]\n";
                    std::cout << "========================================\n";
                    {
                        std::lock_guard<std::mutex> lock(messageHistoryMutex);
                        if (messageHistory.empty()) {
                            std::cout << "No messages received yet.\n";
                        } else {
                            // Display all messages in history
                            for (const auto& msg : messageHistory) {
                                std::cout << msg << "\n";
                            }
                        }
                    }
                    std::cout << "========================================\n";
                    break;
                }
                
                default:
                    std::cout << "\n[Error] Invalid choice. Please enter a number between 1-7.\n";
                    break;
            }
        } else {
            // No input available - sleep briefly to reduce CPU usage
            // 1ms sleep maintains low latency while preventing busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        // ====================================================================
        // Handle Pending Connection Completion
        // ====================================================================
        // Check if a connection attempt has completed (async from option 2)
        if (connectComplete && pendingClientSocket) {
            // Wait for connect thread to finish
            if (clientConnectThreadHandle.joinable()) {
                clientConnectThreadHandle.join();
            }
            
            // Verify connection result by checking socket error status
            int error = 0;
            socklen_t len = sizeof(error);
            bool success = false;
            if (getsockopt(*pendingClientSocket, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                success = true;
            }
            
            if (success && pendingConnectSuccess) {
                // Connection successful - create ClientConnection structure
                int connectionId = pendingConnectionId;
                auto clientConn = std::make_shared<ClientConnection>(connectionId);
                clientConn->socket = pendingClientSocket;
                clientConn->running = true;
                clientConn->connected = true;
                
                // Start receive thread for this connection
                clientConn->receiveThread = std::thread(clientReceiveThread,
                                                        clientConn->socket,
                                                        std::ref(clientConn->running),
                                                        std::ref(clientConn->connected),
                                                        std::ref(clientConn->buffer));
                
                // Add to client connections list (with mutex lock)
                {
                    std::lock_guard<std::mutex> lock(clientConnectionsMutex);
                    clientConnections.push_back(clientConn);
                }
                
                std::cout << "\n[Success] Client connection " << connectionId << " connected to server!\n";
            } else {
                std::cout << "\n[Error] Failed to connect to server.\n";
            }
            
            // Clear pending connection state
            pendingClientSocket.reset();
            pendingConnectionId = 0;
            connectComplete = false;
        }
    }
    
    // ========================================================================
    // Cleanup on Exit
    // ========================================================================
    // Stop all threads by setting control flags
    serverAcceptRunning = false;
    clientConnectRunning = false;
    
    // Close sockets first to break any blocking operations in threads
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
    
    // Wait for all threads to finish
    
    // Join accept thread
    if (serverAcceptThreadHandle.joinable()) {
        serverAcceptThreadHandle.join();
    }
    
    // Join connect thread
    if (clientConnectThreadHandle.joinable()) {
        clientConnectThreadHandle.join();
    }
    
    // Join all server client receive threads
    {
        std::lock_guard<std::mutex> lock(serverClientsMutex);
        for (auto& client : serverClients) {
            if (client->receiveThread.joinable()) {
                client->receiveThread.join();
            }
        }
    }
    
    // Join all client connection receive threads
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
