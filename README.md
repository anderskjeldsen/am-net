# Am-Net

A cross-platform networking library for the Am programming language, providing socket-based communication with SSL/TLS support.

## Overview

Am-Net is a networking library that provides high-level networking functionality for applications written in the Am programming language. It offers both basic socket operations and secure SSL/TLS encrypted connections, with native implementations optimized for multiple platforms including retro and alternative operating systems.

## Features

- **Cross-platform socket support** - Works on AmigaOS, MorphOS, macOS, and other Unix-like systems
- **SSL/TLS encryption** - Secure socket streams with certificate verification
- **Stream interface** - Compatible with Am's IO stream abstraction
- **Multiple socket types** - Support for TCP, UDP, and other socket types
- **Native performance** - C implementations for optimal performance

## Components

### Core Classes

- **Socket** - Low-level socket operations (connect, send, receive, close)
- **SocketStream** - Stream interface wrapper for sockets
- **SslSocketStream** - SSL/TLS encrypted socket streams with certificate verification

### Configuration Enums

- **AddressFamily** - Socket address families (IPv4, IPv6, Unix, etc.)
- **SocketType** - Socket types (stream, datagram, raw, etc.)
- **ProtocolFamily** - Protocol families for socket creation

## Platform Support

Am-Net supports multiple platforms with optimized native implementations:

- **AmigaOS** - Classic Amiga with m68k processor
- **MorphOS** - PowerPC-based Amiga-compatible system
- **macOS** - Modern macOS systems
- **Linux/Unix** - Generic Unix-like systems (via libc)

## Dependencies

- **am-lang-core** - Core Am language runtime and libraries
- **OpenSSL** - For SSL/TLS functionality (platform-dependent)

## Installation

1. Ensure you have the Am language compiler and runtime installed
2. Clone this repository:
   ```bash
   git clone https://github.com/anderskjeldsen/am-net.git
   ```
3. Build using the Am build system (specific instructions depend on your target platform)

## Usage Examples

### Basic TCP Socket

```am
import Am.Net

// Create a TCP socket
var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)

// Connect to a server
socket.connect("example.com", 80)

// Send data
var data = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n".toBytes()
socket.send(data, data.length)

// Receive response
var buffer = new UByte[1024]
var received = socket.receive(buffer, 1024)

// Close connection
socket.close()
```

### Socket Stream

```am
import Am.Net
import Am.IO

// Create socket and wrap in stream
var socket = Socket.create(AddressFamily.inet, SocketType.stream, ProtocolFamily.unspecified)
socket.connect("example.com", 80)

var stream = new SocketStream(socket)

// Use standard stream operations
var data = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n".toBytes()
stream.write(data, 0, data.length)

var buffer = new UByte[1024]
var bytesRead = stream.read(buffer, 0, 1024)
```

### SSL/TLS Support

SSL/TLS functionality is currently implemented at the native C level with certificate verification and hostname validation. The high-level Am language interface for SSL is under development.

```c
// Native C implementation provides:
// - SSL context creation with TLS_client_method()
// - Certificate verification with SSL_get_verify_result()
// - Hostname validation with SSL_set_tlsext_host_name()
// - Secure read/write operations
```

## Building

The project uses a YAML-based build configuration (`package.yml`) that supports multiple target platforms. Building requires the Am language compiler and build tools.

### Build Targets

The following build targets are defined:

- **amigaos_docker** - AmigaOS m68k using Docker with `amiga-gcc` image
- **morphos-ppc_docker** - MorphOS PowerPC using Docker with `amigadev/crosstools:ppc-morphos` image  
- **macos** - Native macOS build

### Prerequisites

- Am language compiler and runtime
- Docker (for cross-compilation targets)
- Platform-specific compilers:
  - `m68k-amigaos-gcc` for AmigaOS
  - `ppc-morphos-gcc` for MorphOS
  - `gcc` for macOS/Unix

### Building

Use the Am build system with your desired target platform. The exact build commands depend on your Am language toolchain setup.

## Architecture

Am-Net follows a layered architecture:

1. **Am Language Layer** (`.aml` files) - High-level API and abstractions
2. **Native C Layer** (`.c` files) - Platform-specific implementations
3. **Platform Abstraction** - Conditional compilation for different platforms

The native implementations are organized by platform:
- `libc/` - Generic Unix/POSIX implementation
- `amigaos/` - AmigaOS-specific code
- `morphos-ppc/` - MorphOS PowerPC implementation
- `macos/` - macOS-specific optimizations

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test on target platforms
5. Submit a pull request

## License

[License information would go here]

## Version

Current version: 0.6.1

See the [package.yml](package.yml) file for detailed dependency and build configuration.