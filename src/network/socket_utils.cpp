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
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

SocketPtr startServer() {
    int serverSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFd < 0) {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return nullptr;
    }

    // SO_REUSEADDR allows binding to address in TIME_WAIT state (rapid restarts)
    int opt = 1;
    setsockopt(serverSocketFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Prevent SIGPIPE (macOS/iOS specific, Linux uses MSG_NOSIGNAL)
    #ifdef SO_NOSIGPIPE
    setsockopt(serverSocketFd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    #endif
    
    // TCP_NODELAY disables Nagle's algorithm for low latency
    opt = 1;
    setsockopt(serverSocketFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    // 64KB buffers reduce syscalls and improve throughput
    int bufferSize = 64 * 1024;
    setsockopt(serverSocketFd, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));
    setsockopt(serverSocketFd, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocketFd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        close(serverSocketFd);
        return nullptr;
    }
    
    // Backlog of 128 helps handle burst connection traffic
    if (listen(serverSocketFd, 128) < 0) {
        std::cerr << "Listen failed: " << strerror(errno) << std::endl;
        close(serverSocketFd);
        return nullptr;
    }
    
    if (!makeNonBlocking(serverSocketFd)) {
        std::cerr << "Failed to make server socket non-blocking: " << strerror(errno) << std::endl;
        close(serverSocketFd);
        return nullptr;
    }

    return SocketPtr(new int(serverSocketFd), [](int* s){
        if (s && *s >= 0) {
            close(*s);
        }
        delete s;
    });
}

SocketPtr startClient() {
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return nullptr;
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // Blocking connect - use clientConnectThread() for non-blocking with timeout
    if (connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Connect failed: " << strerror(errno) << std::endl;
        close(clientSocket);
        return nullptr;
    }

    return SocketPtr(new int(clientSocket), [](int* s){
        if (s && *s >= 0) {
            close(*s);
        }
        delete s;
    });
}
