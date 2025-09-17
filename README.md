# Am-Net

A cross-platform networking library for the Am programming language, providing socket programming capabilities with SSL/TLS support.

## Overview

Am-Net is a networking library that provides high-level socket programming interfaces for the Am programming language. It offers both basic TCP/UDP socket functionality and secure SSL/TLS communications, with native implementations optimized for multiple platforms including AmigaOS, MorphOS, and macOS.

## Features

- **Cross-platform socket support** - Works on AmigaOS, MorphOS, and macOS
- **TCP and UDP socket communication** - Full support for stream and datagram sockets
- **SSL/TLS encryption** - Secure socket streams with OpenSSL integration
- **Native performance** - C implementations for optimal performance
- **Stream interface** - Unified stream API for easy integration with Am IO libraries
- **Multiple address families** - Support for IPv4, IPv6, and Unix domain sockets

## Installation

### Prerequisites

- Am language core library (am-lang-core)
- C compiler (gcc)
- OpenSSL development libraries (for SSL support)

### Platform-specific requirements

#### AmigaOS
- m68k-amigaos-gcc cross-compiler
- socket.library
- Docker environment with amiga-gcc image

#### MorphOS
- ppc-morphos-gcc cross-compiler  
- Docker environment with amigadev/crosstools:ppc-morphos

#### macOS
- Xcode command line tools
- OpenSSL via Homebrew: `brew install openssl`

### Building

1. Clone the repository:
```bash
git clone https://github.com/anderskjeldsen/am-net.git
cd am-net
```

2. Build using the Am language build system (specific instructions depend on your target platform)

## Quick Start

### Basic TCP Socket Example

```am
import Am.Net
import Am.Lang

// Create a TCP socket
var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)

// Connect to a server
socket.connect("example.com", 80)

// Send data
var message = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"
var messageBytes = message.getBytes()
socket.send(messageBytes, messageBytes.length as UInt)

// Receive response
var buffer = new UByte[1024]
var bytesReceived = socket.receive(buffer, 1024u)

// Close the socket
socket.close()
```

### Socket Stream Example

```am
import Am.Net
import Am.IO
import Am.Lang

// Create a socket and wrap it in a stream
var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)
socket.connect("example.com", 80)

var stream = new SocketStream(socket)

// Use stream interface for easier data handling
var request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"
var requestBytes = request.getBytes()
stream.write(requestBytes, 0, requestBytes.length as UInt)

// Read response using stream
var responseBuffer = new UByte[4096]
var bytesRead = stream.read(responseBuffer, 0, 4096u)
```

### SSL/TLS Socket Example

```am
import Am.Net
import Am.IO  
import Am.Lang

// Create a regular socket first
var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)
socket.connect("secure-server.com", 443)

// Wrap in SSL stream for encrypted communication
var sslStream = new SslSocketStream(socket, "secure-server.com")

// Use encrypted stream for secure communication
var httpsRequest = "GET / HTTP/1.1\r\nHost: secure-server.com\r\n\r\n"
var requestBytes = httpsRequest.getBytes()
sslStream.write(requestBytes, 0, requestBytes.length as UInt)

// Read encrypted response
var responseBuffer = new UByte[4096]
var bytesRead = sslStream.read(responseBuffer, 0, 4096u)
```

## API Reference

### Core Classes

#### Socket

The main socket class providing low-level networking capabilities.

**Methods:**
- `static create(addressFamily: AddressFamily, socketType: SocketType, protocolFamily: ProtocolFamily): Socket` - Creates a new socket
- `connect(hostName: String, port: Int)` - Connects to a remote host
- `send(bytes: UByte[], length: UInt): UInt` - Sends data and returns bytes sent
- `receive(bytes: UByte[], length: UInt): UInt` - Receives data and returns bytes received
- `close()` - Closes the socket

#### SocketStream

A stream wrapper around Socket implementing the Stream interface.

**Methods:**
- `read(buffer: UByte[], offset: Long, length: UInt): UInt` - Reads data into buffer
- `write(buffer: UByte[], offset: Long, length: UInt)` - Writes data from buffer
- `seekFromStart(offset: Long)` - Not supported, throws exception
- `readByte(): Int` - Not supported, throws exception  
- `writeByte(byte: Int)` - Not supported, throws exception

#### SslSocketStream

Secure socket stream providing SSL/TLS encryption.

**Constructor:**
- `SslSocketStream(socket: Socket, hostName: String)` - Creates SSL stream from socket

**Methods:**
- Inherits all Stream interface methods
- Automatically handles SSL handshake and encryption/decryption

### Enumerations

#### AddressFamily

Network address families:
- `unspecified = 0` - Unspecified address family
- `unix = 1` - Unix domain sockets  
- `local = 1` - Local communication (alias for unix)
- `inet = 2` - Internet Protocol version 4 (IPv4)
- `inet6 = 10` - Internet Protocol version 6 (IPv6)

#### SocketType  

Socket communication types:
- `stream = 1` - TCP stream sockets (reliable, connection-oriented)
- `dgram = 2` - UDP datagram sockets (unreliable, connectionless)
- `raw = 3` - Raw sockets
- `rdm = 4` - Reliably-delivered messages
- `seqPacket = 5` - Sequenced packet stream

#### ProtocolFamily

Protocol specifications:
- `unspecified = 0` - Let the system choose appropriate protocol

## Platform Support

### AmigaOS
- Native m68k assembly and C implementation
- Requires socket.library
- Built using amiga-gcc Docker container
- Optimized for classic Amiga systems

### MorphOS  
- PowerPC native implementation
- Uses MorphOS SDK and cross-compilation tools
- Built using morphos-ppc Docker container
- Compatible with MorphOS 3.x+

### macOS
- Native macOS implementation using BSD sockets
- OpenSSL integration for SSL support
- Built with standard gcc/clang toolchain
- Compatible with macOS 10.12+

## Error Handling

All networking operations can throw exceptions. Common exception scenarios:

- **Socket creation failure** - Invalid parameters or system limits
- **Connection failure** - Host unreachable, connection refused, timeout
- **SSL handshake failure** - Certificate validation, protocol mismatch
- **Send/receive errors** - Network interruption, buffer issues

Always wrap networking code in try-catch blocks:

```am
try {
    var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)
    socket.connect("example.com", 80)
    // ... networking operations
} catch (e: Exception) {
    // Handle networking errors
    Console.writeLine("Network error: " + e.message)
}
```

## Performance Considerations

- **Buffer sizes** - Use appropriate buffer sizes for your use case (typically 4KB-64KB)
- **Connection reuse** - Reuse connections when possible to avoid handshake overhead
- **SSL overhead** - SSL/TLS adds computational and bandwidth overhead
- **Platform differences** - Performance characteristics vary between platforms

## Troubleshooting

### Common Issues

**"Unable to create socket"**
- Check if the address family and socket type combination is supported
- Verify system socket limits haven't been exceeded
- Ensure proper privileges for raw sockets

**SSL Connection Failures**
- Verify OpenSSL is properly installed and linked
- Check certificate validation and hostname matching
- Ensure SSL/TLS version compatibility

**Build Issues**
- Verify all dependencies are installed for your target platform
- Check Docker container availability for cross-platform builds
- Ensure am-lang-core is properly installed and accessible

### Debug Tips

1. Enable verbose output in socket operations
2. Use network monitoring tools to inspect traffic
3. Test with simple HTTP requests before HTTPS
4. Verify DNS resolution for hostname-based connections

## Contributing

Contributions are welcome! Please follow these guidelines:

1. **Code style** - Follow existing Am language conventions
2. **Testing** - Test on all supported platforms when possible  
3. **Documentation** - Update documentation for new features
4. **Compatibility** - Maintain backward compatibility where possible

### Development Setup

1. Fork the repository
2. Set up development environment for your target platform
3. Make changes and test thoroughly
4. Submit pull request with clear description

## Dependencies

- **am-lang-core** - Core Am language runtime and standard library
- **OpenSSL** - For SSL/TLS support (libssl, libcrypto)
- **Platform-specific socket libraries** - socket.library (AmigaOS), BSD sockets (macOS/MorphOS)

## License

This project follows the same license as the Am language core library. See the am-lang-core repository for license details.

## Related Projects

- [am-lang-core](https://github.com/anderskjeldsen/am-lang-core) - Core Am language implementation
- [Am Programming Language](https://github.com/anderskjeldsen) - Main Am language organization

## Version History

- **0.6.1** - Current version with SSL support and multi-platform compatibility
- Cross-platform socket implementation
- SSL/TLS stream support  
- Stream interface integration

---

For more information about the Am programming language, visit the [main Am language repository](https://github.com/anderskjeldsen/am-lang-core).