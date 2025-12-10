cmake_minimum_required(VERSION 3.10.0)
project(hft-gateway VERSION 0.1.0 LANGUAGES C CXX)

add_executable(hft-gateway
    ./src/main.cpp
    ./src/network/socket_utils.cpp
    ./src/network/message.cpp
    ./src/network/connection.cpp
    ./src/server/server.cpp
    ./src/client/client.cpp
    ./src/ui/ui.cpp
)
