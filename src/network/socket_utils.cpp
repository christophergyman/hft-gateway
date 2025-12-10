#include "socket_utils.h"
#include <cstring>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

bool makeNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

SocketPtr startServer() {
    // create TCP socket
    int serverSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFd < 0) {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return nullptr;
    }

    int opt = 1;
    setsockopt(serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Prevent SIGPIPE
    #ifdef SO_NOSIGPIPE
    setsockopt(serverSocketFd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    #endif
    
    // Disable Nagle's algorithm for low latency (TCP_NODELAY)
    opt = 1;
    setsockopt(serverSocketFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    // Tune socket buffer sizes for performance (64KB each direction)
    int bufferSize = 64 * 1024;
    setsockopt(serverSocketFd, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));
    setsockopt(serverSocketFd, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // bind and listen to socket
    if (bind(serverSocketFd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        close(serverSocketFd);
        return nullptr;
    }
    
    // Increased backlog for burst connection handling
    if (listen(serverSocketFd, 128) < 0) {
        std::cerr << "Listen failed: " << strerror(errno) << std::endl;
        close(serverSocketFd);
        return nullptr;
    }
    
    // Make non-blocking for poll-based accept
    if (!makeNonBlocking(serverSocketFd)) {
        std::cerr << "Failed to make server socket non-blocking: " << strerror(errno) << std::endl;
        close(serverSocketFd);
        return nullptr;
    }

    // manage lifetime of server socket
    return SocketPtr(new int(serverSocketFd), [](int* s){
        if (s && *s >= 0) {
            close(*s);
        }
        delete s;
    });
}

SocketPtr startClient() {
    // creating socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    // specifying address
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // sending connection request
    if (connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Connect failed: " << strerror(errno) << std::endl;
        close(clientSocket);
        return nullptr;
    }

    // manage lifetime of client socket; close on deletion
    return SocketPtr(new int(clientSocket), [](int* s){
        if (s && *s >= 0) {
            close(*s);
        }
        delete s;
    });
}
