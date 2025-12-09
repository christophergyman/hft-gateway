# HFT Gateway

High-performance TCP server/client connection system.

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

Run the executable:
```bash
./build/hft-gateway
```

The system provides an interactive menu:

1. **Create server socket** - Start listening on port 8080
2. **Connect to server** - Connect to server at 127.0.0.1:8080
3. **Send message (server -> client)** - Broadcast message to all connected clients
4. **Send message (client -> server)** - Send message to server
5. **Stop server connection** - Shutdown server and disconnect all clients
6. **Stop client connection** - Disconnect from server
7. **View received messages** - Display queued messages

Messages are automatically displayed when received. The system supports multiple concurrent client connections.
