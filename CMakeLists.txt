cmake_minimum_required(VERSION 3.10.0)
project(hft-gateway VERSION 0.1.0 LANGUAGES C CXX)

add_executable(hft-gateway
    ./src/main.cpp
    ./src/socket_utils.cpp
    ./src/message.cpp
    ./src/connection.cpp
    ./src/server.cpp
    ./src/client.cpp
    ./src/ui.cpp
)
