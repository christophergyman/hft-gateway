#include "connection.h"
#include <unistd.h>

ClientConnection::ClientConnection(int clientId) : id(clientId) {}

ClientConnection::~ClientConnection() {
    running = false;
    connected = false;
    if (socket) {
        close(*socket);
    }
    if (receiveThread.joinable()) {
        receiveThread.join();
    }
}
