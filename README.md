# Am-Net: Networking Library for Am Programming Language

Am-Net is a comprehensive networking library for the Am programming language, providing cross-platform socket functionality with native C implementations.

## Features

### TCP Sockets
- **Basic Socket Operations**: Create, connect, send, receive, and close TCP sockets
- **Connection Management**: Timeout configuration, keep-alive support, and connection state tracking
- **Socket Modes**: Blocking and non-blocking operation modes
- **Data Availability**: Check if data is available for reading without blocking

### UDP Sockets  
- **UDP Socket Support**: Create, bind, send to, and receive from UDP sockets
- **Datagram Operations**: Full support for UDP datagram communication

### Cross-Platform Support
- **AmigaOS**: Native socket support with bsdsocket.library
- **MorphOS**: PowerPC-based MorphOS support
- **macOS**: Native POSIX socket implementation
- **Linux**: Standard POSIX socket implementation

## Usage Examples

### TCP Socket Client
```am
import Am.Net

// Create a TCP socket
var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)

// Connect to server
socket.connect("example.com", 80)

// Send data
var data: UByte[] = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n".getBytes()
socket.send(data, data.length)

// Receive response
var buffer: UByte[1024]
var received = socket.receive(buffer, buffer.length)

// Close connection
socket.close()
```

### UDP Socket
```am
import Am.Net

// Create UDP socket
var udpSocket = UdpSocket.create(AddressFamily.inet)

// Send datagram
var data: UByte[] = "Hello UDP".getBytes()
udpSocket.sendTo(data, data.length, "udp.example.com", 1234)

// Receive response
var buffer: UByte[1024]
var received = udpSocket.receiveFrom(buffer, buffer.length)

// Close socket
udpSocket.close()
```

### Advanced Socket Configuration
```am
import Am.Net

var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)

// Configure socket options
socket.setTimeout(5000)  // 5 second timeout
socket.setMode(SocketMode.nonBlocking)  // Non-blocking mode
socket.setKeepAlive(true)  // Enable keep-alive

// Connect with timeout
socket.connectWithTimeout("example.com", 80, 3000)

// Check connection state
if (socket.isConnected()) {
    // Check if data is available
    if (socket.isDataAvailable()) {
        var buffer: UByte[1024]
        var received = socket.receive(buffer, buffer.length)
    }
}
```

## Building

The library supports multiple build targets using Docker containers:

```bash
# Build for AmigaOS
docker build -t amiga-gcc .
docker run -v $(pwd):/host amiga-gcc

# Build for MorphOS  
docker run -v $(pwd):/work amigadev/crosstools:ppc-morphos

# Build for macOS (native)
make macos
```

## API Reference

### Socket Class
- `static create(AddressFamily, SocketType, ProtocolFamily): Socket`
- `connect(hostName: String, port: Int)`
- `connectWithTimeout(hostName: String, port: Int, timeoutMs: Int)`
- `send(bytes: UByte[], length: UInt): UInt`
- `receive(bytes: UByte[], length: UInt): UInt`
- `setTimeout(timeoutMs: Int)`
- `setMode(mode: SocketMode)`
- `setKeepAlive(enabled: Bool)`
- `isDataAvailable(): Bool`
- `close()`
- `getConnectionState(): ConnectionState`
- `isConnected(): Bool`

### UdpSocket Class
- `static create(AddressFamily): UdpSocket`
- `bind(port: Int)`
- `sendTo(bytes: UByte[], length: UInt, hostName: String, port: Int): UInt`
- `receiveFrom(bytes: UByte[], length: UInt): UInt`
- `close()`

### Enums
- `AddressFamily`: unspecified, unix, local, inet, inet6
- `SocketType`: stream, dgram, raw, rdm, seqPacket
- `ProtocolFamily`: unspecified
- `SocketMode`: blocking, nonBlocking
- `ConnectionState`: disconnected, connecting, connected, error

## Dependencies

- **am-lang-core**: Core Am language runtime
- **bsdsocket.library**: For AmigaOS socket support

## License

This library is part of the Am programming language ecosystem.

## Contributing

Contributions are welcome! Please submit issues and pull requests on the GitHub repository.