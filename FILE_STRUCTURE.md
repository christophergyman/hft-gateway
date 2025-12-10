# HFT Gateway - File Structure Reference

## Directory Structure

```
src/
├── main.cpp                    # Application entry point and control loop
├── network/                    # Core networking components
│   ├── socket_utils.h/cpp     # Socket operations and utilities
│   ├── message.h/cpp          # Message framing, buffering, and transmission
│   └── connection.h/cpp       # Client connection management
├── server/                     # Server-side components
│   └── server.h/cpp           # Server-side thread functions
├── client/                     # Client-side components
│   └── client.h/cpp          # Client-side thread functions
└── ui/                         # User interface components
    └── ui.h/cpp               # User interface and menu handling
```

## Module Reference

### `network/socket_utils.h/cpp`

**Type Definitions:**
```cpp
using SocketPtr = std::shared_ptr<int>;
```

**Functions:**

#### `bool makeNonBlocking(int fd)`
Sets a socket file descriptor to non-blocking mode.

**Parameters:**
- `fd` - Socket file descriptor

**Returns:** `true` on success, `false` on error

**Usage:**
```cpp
int sock = socket(AF_INET, SOCK_STREAM, 0);
makeNonBlocking(sock);
```

---

#### `SocketPtr startServer()`
Creates and configures a TCP server socket listening on port 8080.

**Returns:** `SocketPtr` on success, `nullptr` on error

**Features:**
- Sets `SO_REUSEADDR` option
- Sets `SO_NOSIGPIPE` (if available)
- Configures socket as non-blocking
- Binds to `INADDR_ANY:8080`
- Sets listen backlog to 128 (for burst connection handling)
- Disables Nagle's algorithm (`TCP_NODELAY`) for low latency
- Tunes socket buffer sizes (`SO_RCVBUF` and `SO_SNDBUF` to 64KB each)

**Usage:**
```cpp
SocketPtr server = startServer();
if (!server) {
    // Handle error
}
```

---

#### `SocketPtr startClient()`
Creates a TCP client socket and connects to `INADDR_ANY:8080`.

**Returns:** `SocketPtr` on success, `nullptr` on error

**Note:** This function performs a blocking connect. For non-blocking connections, use `clientConnectThread()`.

**Usage:**
```cpp
SocketPtr client = startClient();
if (!client) {
    // Handle error
}
```

---

### `network/message.h/cpp`

**Classes:**

#### `MessageBuffer`
Handles length-prefixed message buffering for partial reads. Optimized to avoid memory copies by tracking read position instead of using `substr()` and `erase()`.

**Public Methods:**

```cpp
bool addData(const char* data, size_t len);
```
Adds received data to the internal buffer. Automatically compacts buffer when needed to prevent unbounded growth.

```cpp
bool extractMessage(std::string& message);
```
Extracts a complete message from the buffer if available. Returns `false` if message is incomplete. Uses read position tracking to avoid memory copies.

**Message Format:** `[4 bytes: length (network byte order)][N bytes: payload]`

**Max Message Size:** 1MB

**Implementation Details:**
- Uses read position tracking (`readPos_`) to avoid `substr()` and `erase()` operations
- Automatically compacts buffer when read position exceeds half the buffer size or buffer exceeds 1MB
- Thread-safe when used per-connection (each connection has its own `MessageBuffer` instance)

```cpp
void clear();
```
Clears the internal buffer and resets read position.

**Usage:**
```cpp
MessageBuffer buffer;
char data[1024];
ssize_t received = recv(sock, data, sizeof(data), 0);
buffer.addData(data, received);

std::string message;
if (buffer.extractMessage(message)) {
    // Process complete message
}
```

---

#### `MessageQueue`
Thread-safe message queue for inter-thread communication.

**Public Methods:**

```cpp
void push(const std::string& source, const std::string& message);
```
Adds a message to the queue with source identifier.

```cpp
bool pop(std::string& source, std::string& message);
```
Retrieves a message from the queue. Returns `false` if queue is empty.

```cpp
void clear();
```
Removes all messages from the queue.

**Global Instance:**
```cpp
extern MessageQueue receivedMessages;
```

**Usage:**
```cpp
// Producer thread
receivedMessages.push("Client", "Hello from client");

// Consumer thread
std::string source, msg;
while (receivedMessages.pop(source, msg)) {
    std::cout << "[" << source << "] " << msg << std::endl;
}
```

---

**Functions:**

#### `bool sendFramedMessage(int socketFd, const std::string& message)`
Sends a length-prefixed message over a socket.

**Parameters:**
- `socketFd` - Socket file descriptor
- `message` - Message payload

**Returns:** `true` on success, `false` on error

**Features:**
- Non-blocking send with poll() timeout (1ms for low latency)
- Handles partial writes
- Uses `MSG_NOSIGNAL` if available

**Usage:**
```cpp
std::string msg = "Hello, World!";
if (!sendFramedMessage(sock, msg)) {
    // Handle error
}
```

---

#### `bool sendToClient(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message)`
Sends a message from server to client.

**Parameters:**
- `clientSocket` - Client socket pointer
- `message` - Shared pointer to message string

**Returns:** `true` on success, `false` on error

---

#### `bool sendToServer(const SocketPtr& clientSocket, const std::shared_ptr<const std::string>& message)`
Sends a message from client to server.

**Parameters:**
- `clientSocket` - Client socket pointer
- `message` - Shared pointer to message string

**Returns:** `true` on success, `false` on error

---

#### `bool receiveFramedMessage(int socketFd, MessageBuffer& buffer, std::string& message)`
Receives and extracts a complete framed message.

**Parameters:**
- `socketFd` - Socket file descriptor
- `buffer` - Message buffer instance
- `message` - Output parameter for extracted message

**Returns:** `true` if complete message extracted, `false` otherwise

**Features:**
- Non-blocking receive with poll() timeout (1ms for low latency)
- Handles partial reads
- Uses internal 8KB receive buffer (reduced syscalls for large messages)

**Usage:**
```cpp
MessageBuffer buffer;
std::string message;
if (receiveFramedMessage(sock, buffer, message)) {
    // Process message
}
```

---

#### `bool receiveFromClient(const SocketPtr& clientSocket, std::string& message)`
Legacy wrapper for receiving from client (server side).

**Note:** Creates a temporary buffer internally (thread-safe but inefficient). For proper per-connection buffering, use `receiveFramedMessage()` directly with a per-connection `MessageBuffer`.

---

#### `bool receiveFromServer(const SocketPtr& clientSocket, std::string& message)`
Legacy wrapper for receiving from server (client side).

**Note:** Creates a temporary buffer internally (thread-safe but inefficient). For proper per-connection buffering, use `receiveFramedMessage()` directly with a per-connection `MessageBuffer`.

---

### `network/connection.h/cpp`

**Types:**

```cpp
struct ClientConnection {
    SocketPtr socket;
    std::thread receiveThread;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    MessageBuffer buffer;
    int id;
    
    ClientConnection(int clientId);
    ~ClientConnection();
};

using ClientConnectionPtr = std::shared_ptr<ClientConnection>;
```

**Fields:**
- `socket` - Socket pointer for the connection
- `receiveThread` - Thread handle for receive operations
- `running` - Atomic flag indicating thread should continue
- `connected` - Atomic flag indicating connection is active
- `buffer` - Per-connection message buffer
- `id` - Unique client identifier

**Usage:**
```cpp
auto client = std::make_shared<ClientConnection>(1);
client->socket = socketPtr;
client->running = true;
client->receiveThread = std::thread(serverReceiveThread, client);
```

---

### `server/server.h/cpp`

**Functions:**

#### `void serverReceiveThread(ClientConnectionPtr clientConn)`
Thread function for receiving messages from a client connection.

**Parameters:**
- `clientConn` - Shared pointer to client connection

**Behavior:**
- Sets `connected` flag to `true` on start
- Continuously receives messages and pushes to `receivedMessages` queue
- Detects disconnections via poll() checking for `POLLERR` or `POLLHUP`
- Sets `connected` to `false` on exit

**Usage:**
```cpp
auto client = std::make_shared<ClientConnection>(id);
client->socket = socketPtr;
client->running = true;
client->receiveThread = std::thread(serverReceiveThread, client);
```

---

#### `void serverAcceptThread(SocketPtr serverSocket, 
                        std::atomic<bool>& running, 
                        std::vector<ClientConnectionPtr>& clients,
                        std::mutex& clientsMutex, 
                        std::atomic<int>& nextClientId,
                        size_t maxConnections = 1000)`
Thread function for accepting new client connections.

**Parameters:**
- `serverSocket` - Server socket pointer
- `running` - Atomic flag to control thread execution
- `clients` - Vector of client connections (protected by mutex)
- `clientsMutex` - Mutex for clients vector
- `nextClientId` - Atomic counter for client IDs
- `maxConnections` - Maximum number of concurrent connections (default: 1000)

**Behavior:**
- Non-blocking accept loop using poll() with 1ms timeout (low latency)
- Cleans up disconnected clients before checking connection limits
- Rejects new connections if at maximum limit (closes socket immediately)
- Creates `ClientConnection` for each accepted client
- Configures client socket as non-blocking
- Sets `TCP_NODELAY` and tunes buffer sizes (64KB each direction) on accepted sockets
- Spawns `serverReceiveThread` for each client
- Adds client to clients vector (with mutex lock)
- Pushes connection notification to `receivedMessages` queue

**Usage:**
```cpp
std::atomic<bool> running(true);
std::vector<ClientConnectionPtr> clients;
std::mutex clientsMutex;
std::atomic<int> nextId(1);

std::thread acceptThread(serverAcceptThread,
                         serverSocket,
                         std::ref(running),
                         std::ref(clients),
                         std::ref(clientsMutex),
                         std::ref(nextId),
                         1000); // Max connections
```

---

### `client/client.h/cpp`

**Functions:**

#### `void clientReceiveThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running, 
                        std::atomic<bool>& connected, 
                        MessageBuffer& buffer)`
Thread function for receiving messages from server.

**Parameters:**
- `clientSocket` - Client socket pointer
- `running` - Atomic flag to control thread execution
- `connected` - Atomic flag indicating connection status
- `buffer` - Message buffer instance

**Behavior:**
- Sets `connected` to `true` on start
- Continuously receives messages and pushes to `receivedMessages` queue
- Detects disconnections via poll() checking for `POLLERR` or `POLLHUP`
- Sets `connected` to `false` on exit

**Usage:**
```cpp
MessageBuffer buffer;
std::atomic<bool> running(true);
std::atomic<bool> connected(false);

std::thread recvThread(clientReceiveThread,
                       clientSocket,
                       std::ref(running),
                       std::ref(connected),
                       std::ref(buffer));
```

---

#### `void clientConnectThread(SocketPtr clientSocket, 
                        std::atomic<bool>& running,
                        std::atomic<bool>& connectComplete, 
                        bool& connectSuccess,
                        const std::string& serverAddr = "127.0.0.1", 
                        int timeoutSeconds = 5)`
Thread function for non-blocking connection with timeout.

**Parameters:**
- `clientSocket` - Client socket pointer (must be created but not connected)
- `running` - Atomic flag to control thread execution
- `connectComplete` - Atomic flag set to `true` when connection attempt finishes
- `connectSuccess` - Reference to boolean set to connection result
- `serverAddr` - Server IP address (default: "127.0.0.1")
- `timeoutSeconds` - Connection timeout in seconds (default: 5)

**Behavior:**
- Makes socket non-blocking
- Sets `TCP_NODELAY` and tunes buffer sizes (64KB each direction) for low latency
- Initiates non-blocking connect
- Uses poll() to wait for connection completion
- Checks socket error status via `getsockopt(SO_ERROR)`
- Sets `connectComplete` and `connectSuccess` on completion

**Usage:**
```cpp
int sock = socket(AF_INET, SOCK_STREAM, 0);
SocketPtr clientSocket(new int(sock), [](int* s) { close(*s); delete s; });

std::atomic<bool> running(true);
std::atomic<bool> complete(false);
bool success = false;

std::thread connectThread(clientConnectThread,
                          clientSocket,
                          std::ref(running),
                          std::ref(complete),
                          std::ref(success),
                          "127.0.0.1",
                          5);

// Wait for completion
while (!complete) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
connectThread.join();

if (success) {
    // Connection successful
}
```

---

### `ui/ui.h/cpp`

**Functions:**

#### `void displayMenu(SocketPtr serverSocket, 
                 const std::vector<ClientConnectionPtr>& clients,
                 SocketPtr clientSocket, 
                 std::atomic<bool>& clientConnected)`
Displays the main control menu with current status.

**Parameters:**
- `serverSocket` - Server socket pointer (nullptr if not running)
- `clients` - Vector of connected clients
- `clientSocket` - Client socket pointer (nullptr if not connected)
- `clientConnected` - Atomic flag indicating client connection status

**Menu Options:**
1. Create server socket
2. Connect to server
3. Send message (server -> client)
4. Send message (client -> server)
5. Stop server connection
6. Stop client connection
7. View received messages

**Usage:**
```cpp
displayMenu(serverSocket, clients, clientSocket, clientConnected);
```

---

#### `bool hasInput()`
Non-blocking check for stdin input availability.

**Returns:** `true` if input is available, `false` otherwise

**Implementation:** Uses `poll()` on `STDIN_FILENO` with 0ms timeout

**Usage:**
```cpp
if (hasInput()) {
    std::string input;
    std::getline(std::cin, input);
    // Process input
}
```

---

### `main.cpp`

**Application Entry Point**

**Main Loop:**
1. Checks for received messages (non-blocking)
2. Cleans up disconnected clients
3. Displays menu and handles input (if available)
4. Processes menu selections (1-7)
5. Handles client connection completion
6. Sleeps 1ms if no input available (low latency message processing)

**Menu Handlers:**

**Option 1 - Create Server:**
- Creates server socket via `startServer()`
- Spawns `serverAcceptThread` with max connections limit (1000)
- Initializes client ID counter

**Option 2 - Connect to Server:**
- Creates client socket
- Spawns `clientConnectThread` in background
- Connection result handled asynchronously

**Option 3 - Send Server->Client:**
- Prompts for message
- Sends to all connected clients via `sendToClient()`

**Option 4 - Send Client->Server:**
- Prompts for message
- Sends to server via `sendToServer()`

**Option 5 - Stop Server:**
- Stops accept thread
- Closes server socket
- Stops all client receive threads
- Joins all threads
- Clears clients vector

**Option 6 - Stop Client:**
- Stops receive thread
- Closes client socket
- Stops connect thread (if running)
- Joins threads
- Clears message buffer

**Option 7 - View Messages:**
- Pops and displays all queued messages from `receivedMessages`

**Cleanup:**
- Closes all sockets
- Stops all threads
- Joins all threads
- Clears all resources

---

## Build Configuration

**CMakeLists.txt:**
```cmake
add_executable(hft-gateway
    ./src/main.cpp
    ./src/network/socket_utils.cpp
    ./src/network/message.cpp
    ./src/network/connection.cpp
    ./src/server/server.cpp
    ./src/client/client.cpp
    ./src/ui/ui.cpp
)
```

**Compilation:**
```bash
mkdir build && cd build
cmake ..
make
```

---

## Module Dependencies

```
network/socket_utils.h/cpp
    └── (no dependencies)

network/message.h/cpp
    └── network/socket_utils.h

network/connection.h/cpp
    ├── network/socket_utils.h
    └── network/message.h

server/server.h/cpp
    ├── network/socket_utils.h
    ├── network/connection.h
    └── network/message.h

client/client.h/cpp
    ├── network/socket_utils.h
    └── network/message.h

ui/ui.h/cpp
    ├── network/socket_utils.h
    └── network/connection.h

main.cpp
    └── (all modules)
```

---

## Protocol Specification

**Message Format:**
```
[4 bytes: length (uint32_t, network byte order)][N bytes: payload]
```

**Constraints:**
- Maximum message size: 1MB (1,048,576 bytes)
- Length field uses network byte order (big-endian)
- Empty messages are not allowed

**Example:**
```
Message: "Hello"
Length: 5 (0x00000005 in network byte order)
Frame: [0x00 0x00 0x00 0x05][0x48 0x65 0x6C 0x6C 0x6F]
```

---

## Threading Model

**Server Side:**
- 1 accept thread (`serverAcceptThread`)
- N receive threads (one per client, `serverReceiveThread`)

**Client Side:**
- 1 connect thread (`clientConnectThread`) - temporary
- 1 receive thread (`clientReceiveThread`) - after connection

**Main Thread:**
- Menu loop
- Message display
- Client cleanup

**Synchronization:**
- `MessageQueue` uses mutex for thread-safe operations
- Client vector protected by `clientsMutex`
- Atomic flags for thread control and status

---

## Error Handling

**Socket Errors:**
- Functions return `nullptr` or `false` on error
- Error messages printed to `std::cerr`
- Uses `strerror(errno)` for error descriptions

**Connection Errors:**
- Detected via `poll()` events (`POLLERR`, `POLLHUP`)
- Status flags set to `false`
- Disconnection messages pushed to `receivedMessages` queue

**Message Errors:**
- Invalid message size (>1MB) clears buffer
- Incomplete messages remain in buffer
- Send/receive failures return `false`

---

## Constants

**Network:**
- Default port: `8080`
- Default server address: `"127.0.0.1"`
- Default connection timeout: `5 seconds`

**Timeouts:**
- Poll timeout: `1ms` (optimized for low latency)
- Main loop sleep: `1ms` (optimized for low latency)

**Limits:**
- Maximum message size: `1MB`
- Receive buffer size: `8KB` (reduced syscalls)
- Server listen backlog: `128` (burst connection handling)
- Maximum concurrent connections: `1000` (configurable in `serverAcceptThread`)

## Performance Optimizations

The codebase has been optimized for high-performance, low-latency operation:

**Socket Optimizations:**
- `TCP_NODELAY` enabled on all sockets (disables Nagle's algorithm for minimal latency)
- Socket buffer sizes: `64KB` each direction (`SO_RCVBUF`, `SO_SNDBUF`) for better throughput
- Increased listen backlog to `128` for handling burst connection traffic

**I/O Optimizations:**
- All poll operations use `1ms` timeout for minimal latency (reduced from 100ms)
- Main loop sleep reduced to `1ms` (from 50ms) for faster message processing
- Receive buffer increased to `8KB` (from 1KB) to reduce syscalls for large messages

**Memory Optimizations:**
- `MessageBuffer` uses read position tracking instead of `substr()`/`erase()` to avoid memory copies
- Automatic buffer compaction prevents unbounded growth
- Thread-safe per-connection buffers (removed static buffers that caused race conditions)

**Resource Management:**
- Connection limits prevent resource exhaustion (max 1000 concurrent connections)
- Automatic cleanup of disconnected clients before checking connection limits
