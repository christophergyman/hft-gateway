#pragma once

#include "socket_utils.h"
#include "connection.h"
#include <vector>
#include <atomic>

// Display the main menu
void displayMenu(SocketPtr serverSocket, 
                 const std::vector<ClientConnectionPtr>& serverClients,
                 const std::vector<ClientConnectionPtr>& clientConnections);

// Check if stdin has input available (non-blocking)
bool hasInput();
