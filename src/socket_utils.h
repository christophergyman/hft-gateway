#pragma once

#include <memory>
#include <string>

using SocketPtr = std::shared_ptr<int>;

// Helper function to make socket non-blocking
bool makeNonBlocking(int fd);

// Create server socket and listen (non-blocking, doesn't accept)
SocketPtr startServer();

// Create client socket (blocking connect)
SocketPtr startClient();
