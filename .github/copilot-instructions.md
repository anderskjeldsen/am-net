# AmLang Networking Library (am-net)

This repository contains the networking library for the AmLang programming language ecosystem. AmLang is a modern object-oriented programming language designed for systems programming with cross-platform compatibility, particularly targeting niche platforms like AmigaOS, MorphOS, and legacy systems.

## Repository Overview

**am-net** provides comprehensive TCP/UDP socket programming capabilities for AmLang applications. The library is designed to work seamlessly across all AmLang-supported platforms while maintaining high performance and providing both client and server networking functionality.

### Key Features

- **Cross-Platform Sockets**: TCP and UDP socket support across all AmLang platforms
- **Client & Server Support**: Both client connections and server socket binding/listening
- **Stream Integration**: `SocketStream` class integrates with AmLang's I/O stream system
- **Type-Safe API**: Strong typing with enums for address families, socket types, and protocols
- **Native Performance**: Direct system socket API integration for optimal performance
- **Memory Efficient**: Designed for embedded and resource-constrained environments

### Core Classes

- **`Socket`**: Main socket class for TCP/UDP networking operations
- **`SocketStream`**: Stream wrapper for socket I/O operations
- **`AddressFamily`**: Enumeration for network address families (IPv4, IPv6, etc.)
- **`SocketType`**: Enumeration for socket types (TCP stream, UDP datagram)
- **`ProtocolFamily`**: Enumeration for protocol families

## API Usage Patterns

### TCP Client Connection

```amlang
import Am.Net
import Am.Lang

// Create TCP socket
var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.tcp)

// Connect to server
socket.connect("example.com", 80)

// Send data
var message = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"
var bytes = message.getBytes("UTF-8")
socket.send(bytes, bytes.length())

// Receive response
var buffer: UByte[] = new UByte[1024]
var received = socket.receive(buffer, 1024UI)

// Close connection
socket.close()
```

### TCP Server Socket

```amlang
import Am.Net
import Am.Lang

// Create server socket
var serverSocket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.tcp)

// Bind to port
serverSocket.bind(8080)

// Listen for connections
serverSocket.listen(10)  // backlog of 10 connections

// Accept client connections
while (true) {
    var clientSocket = serverSocket.accept()
    
    // Handle client in separate thread or process request
    handleClient(clientSocket)
    
    clientSocket.close()
}

serverSocket.close()
```

### Using SocketStream for I/O

```amlang
import Am.Net
import Am.IO
import Am.Lang

var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.tcp)
socket.connect("example.com", 80)

// Wrap socket in stream for easier I/O
var socketStream = new SocketStream(socket)
var textStream = new TextStream(socketStream)

// Send text data
textStream.writeLine("GET / HTTP/1.1")
textStream.writeLine("Host: example.com")
textStream.writeLine("")

// Read response
var response = textStream.readLine()
response.println()

socket.close()
```

### UDP Socket Communication

```amlang
import Am.Net
import Am.Lang

// Create UDP socket
var udpSocket = Socket.create(AddressFamily.inet, SocketType.datagram, ProtocolFamily.udp)

// Send UDP packet
var data = "Hello UDP World!".getBytes("UTF-8")
udpSocket.sendTo("127.0.0.1", 8080, data, data.length())

// Receive UDP packet
var buffer: UByte[] = new UByte[1024]
var received = udpSocket.receiveFrom(buffer, 1024UI)

udpSocket.close()
```

## Project Structure

```
src/am-lang/Am/Net/
├── Socket.aml              # Main socket class with native methods
├── SocketStream.aml        # Stream wrapper for socket I/O
├── AddressFamily.aml       # Network address family enumeration
├── SocketType.aml          # Socket type enumeration (TCP/UDP)
└── ProtocolFamily.aml      # Protocol family enumeration

src/native-c/
├── libc/Am/Net/           # Common POSIX socket implementation
├── linux-x64/Am/Net/     # Linux-specific socket code
├── macos/Am/Net/          # macOS-specific socket code  
├── amigaos/Am/Net/        # AmigaOS-specific socket code
└── morphos-ppc/Am/Net/    # MorphOS-specific socket code

examples/
├── http-server/           # Simple HTTP server example
├── http-server-json/      # HTTP server with JSON support
└── tcp-client/            # Basic TCP client example
```

## Building and Testing

### Prerequisites

- AmLang compiler (`amlc`) v0.6.1 or later
- AmLang core library (`am-lang-core`) as dependency
- Platform-specific development tools for cross-compilation

### Build Commands

```bash
# Build the library for current platform
java -jar amlc.jar build . -bt linux-x64

# Build for AmigaOS (requires Docker)
java -jar amlc.jar build . -bt amigaos_docker

# Build for MorphOS (requires Docker)  
java -jar amlc.jar build . -bt morphos-ppc_docker

# Build for macOS
java -jar amlc.jar build . -bt macos
```

### Testing

```bash
# Run socket tests
cd examples/tcp-client
java -jar ../../amlc.jar build . -bt linux-x64
cd builds/bin/linux-x64 && ./app

# Test HTTP server
cd examples/http-server
java -jar ../../amlc.jar build . -bt linux-x64
cd builds/bin/linux-x64 && ./app &
curl http://localhost:8080/
```

## Development Guidelines

### Critical Socket Usage Patterns

1. **Always Use Static Factory Method**:
   ```amlang
   // CORRECT:
   var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.tcp)
   
   // WRONG:
   var socket = new Socket()  // Doesn't initialize native socket
   ```

2. **Resource Management**:
   ```amlang
   var socket = Socket.create(...)
   try {
       // Use socket...
   } finally {
       socket.close()  // Always close sockets
   }
   ```

3. **Error Handling**:
   ```amlang
   try {
       socket.connect("invalid-host", 80)
   } catch (e: Exception) {
       "Connection failed: ${e.message}".println()
   }
   ```

### Server Socket Patterns

```amlang
// Server setup
var server = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.tcp)
server.bind(8080)
server.listen(10)

// Accept loop
while (true) {
    try {
        var client = server.accept()
        // Process client...
        client.close()
    } catch (e: Exception) {
        "Error accepting client: ${e.message}".println()
    }
}
```

### Platform-Specific Considerations

- **AmigaOS**: Requires `-lsocket` library linking
- **MorphOS**: Uses standard BSD socket API
- **Linux/macOS**: Full POSIX socket support
- **Embedded Platforms**: Limited concurrent connection support

## Platform Support

| Platform | Status | Socket Types | Notes |
|----------|---------|--------------|-------|
| Linux x64 | ✅ Full | TCP, UDP | Primary development platform |
| macOS | ✅ Full | TCP, UDP | Intel and Apple Silicon |
| AmigaOS 68k | ✅ Full | TCP, UDP | Via Docker cross-compilation |
| MorphOS PPC | ✅ Full | TCP, UDP | Via Docker cross-compilation |
| AROS x86-64 | 🚧 Partial | TCP | Limited testing |
| Windows x64 | ❌ None | - | Requires Winsock implementation |

## Dependencies

- **am-lang-core**: Core AmLang standard library
  - `Am.Lang.*`: Basic types and functionality
  - `Am.IO.Stream`: Base stream interface for SocketStream
  - Memory management and reference counting

## Socket API Reference

### Socket Creation and Configuration

```amlang
// TCP socket
Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.tcp)

// UDP socket  
Socket.create(AddressFamily.inet, SocketType.datagram, ProtocolFamily.udp)
```

### Client Operations

```amlang
socket.connect(hostname: String, port: Int)        // Connect to remote host
socket.send(data: UByte[], length: UInt): UInt     // Send data
socket.receive(buffer: UByte[], length: UInt): UInt // Receive data
socket.close()                                      // Close connection
```

### Server Operations

```amlang
socket.bind(port: Int)                             // Bind to local port
socket.listen(backlog: Int)                        // Listen for connections
socket.accept(): Socket                            // Accept client connection
```

## Common Issues and Solutions

### Connection Refused
**Problem**: "Connection refused" when connecting to server
**Solution**: Verify server is running and port is correct, check firewall settings

### Socket Creation Failed
**Problem**: "Unable to create socket" error
**Solution**: Check platform support, ensure proper AddressFamily/SocketType combination

### Memory Leaks
**Problem**: Sockets not being cleaned up properly
**Solution**: Always call `socket.close()` in finally blocks or use proper resource management

### Cross-Platform Socket Differences
**Problem**: Code works on Linux but fails on AmigaOS
**Solution**: Test on target platforms, use Docker builds for exotic platforms

## Examples and Learning Resources

1. **http-server**: Complete HTTP server implementation
2. **http-server-json**: HTTP server with JSON request/response handling
3. **tcp-client**: Basic TCP client for testing connections
4. **Socket class documentation**: Inline documentation in Socket.aml

## Contributing Guidelines

1. **Cross-Platform Compatibility**: Test on multiple platforms when possible
2. **Native Code**: Be careful with platform-specific socket APIs
3. **Memory Management**: Ensure proper reference counting in native code
4. **Performance**: Consider embedded/legacy platform limitations
5. **Documentation**: Update examples for API changes

## Version Compatibility

- **AmLang Compiler**: Requires v0.6.1+ for proper native socket support
- **am-lang-core**: Must be compatible version with Stream interface
- **Platform SDKs**: Requires appropriate GCC/socket libraries for each platform

## License

This library is part of the AmLang ecosystem and follows the same MIT license.

---

This library is part of the AmLang ecosystem. For broader context and cross-repository development, see the main AmLang workspace documentation.