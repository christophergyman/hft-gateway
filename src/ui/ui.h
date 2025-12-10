#pragma once

/**
 * @file ui.h
 * @brief User interface and menu handling functions
 */

#include "../network/socket_utils.h"
#include "../network/connection.h"
#include <vector>
#include <atomic>

/**
 * @brief Displays main control menu with current server/client status
 */
void displayMenu(SocketPtr serverSocket, 
                 const std::vector<ClientConnectionPtr>& serverClients,
                 const std::vector<ClientConnectionPtr>& clientConnections);

/**
 * @brief Non-blocking check for stdin input availability
 */
bool hasInput();
