# XRCON Protocol Specification

## Overview

XRCON is a TCP-based remote console protocol implemented as a server within the engine. It provides an alternative to the legacy RCON (which operates over UDP as out-of-band packets). XRCON uses a framed binary protocol over a single persistent TCP connection, accepting at most one client at a time.

Key characteristics:

- Based on TCP (stream-oriented, reliable)
- Default port is 27000
- Maximum only one concurrent client
- Binary framed protocol with a 12-byte header + variable-length payload



## Configuration

The XRCON server is controlled by two configuration variables, both restricted to privileged users:

| Variable | Default | Description |
|---|---|---|
| `xrcon_enable` | `0` (disabled) | Master switch; when enabled, the server starts listening for connections |
| `xrcon_address` | `127.0.0.1:27000` | Bind address and port; supports both IPv4 and IPv6. Changing this at runtime triggers a XRCON server restart (stop + rebind) |

By default, XRCON binds only to localhost (`127.0.0.1`), providing a degree of access control. The server will not start listening until explicitly enabled.



## Frame Format

All XRCON messages are encapsulated in a framed binary format. Each frame consists of a fixed 12-byte header followed by a variable-length payload.

### Header (12 bytes)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | Type | 4-character message type string (null-terminated, occupies 5 bytes in the structure but the 5th byte is padding) |
| 4 | 4 | Version | Protocol version (32-bit unsigned integer, network byte order) |
| 8 | 2 | Length | Total frame length in bytes, including the header (16-bit unsigned integer, network byte order) |
| 10 | 2 | Handle | Handle or sequence number (16-bit unsigned integer, network byte order; currently always zero) |


### Protocol Version

The current protocol version is `0x000000D4` (212 decimal). The server also accepts `0x00D40000` for compatibility with certain third-party clients (e.g., CS2RemoteConsole).

### Maximum Frame Size

- Maximum payload per frame: 4096 bytes
- Maximum total packet size (header + payload): approximately 4136 bytes
- Receive buffer size: 4096 + 64 bytes
- Transmit buffer size: 16384 bytes (4 × maximum frame size)

### Byte Order

Multi-byte integer fields (version, length, handle) are transmitted in **network byte order** (big-endian).



## Message Types

| Type | Direction | Description |
|---|---|---|
| `CMND` | Client → Server | Execute a console command |
| `PRNT` | Server → Client | Console output / print message |
| `CHAN` | Server → Client | Channel list (channel metadata) |
| `AINF` | Server → Client | Application / server info |
| `ADON` | Server → Client | Additional info / server name |

### CMND — Client Command

**Direction**: Client → Server

Sent by the client to execute a console command on the server.

| Section | Size | Description |
|---|---|---|
| Header | 12 bytes | Type = `"CMND"`, version, length, handle |
| Payload | Variable | Raw command string (null-terminated) |

The command string is injected into the engine's command buffer for execution. There is **no authentication** — any connected client may execute arbitrary commands.

### PRNT — Server Print

**Direction**: Server → Client

Sent by the server to stream console output to the connected client.

| Section | Size | Description |
|---|---|---|
| Header | 12 bytes | Type = `"PRNT"`, version, length, handle |
| Channel ID | 4 bytes | Channel identifier (unsigned 32-bit integer; always 0 for "Console") |
| Padding | 20 bytes | Reserved (5 × 32-bit unsigned integers, all zero) |
| Red | 1 byte | Red color component (always 255) |
| Green | 1 byte | Green color component (always 255) |
| Blue | 1 byte | Blue color component (always 255) |
| Alpha | 1 byte | Alpha / opacity component (always 255) |
| Text | Variable | The console output text (up to 4096 bytes) |


### CHAN — Channel List

**Direction**: Server → Client

Sent by the server during the initial handshake (and potentially later) to describe the available console output channels.

| Section | Size | Description |
|---|---|---|
| Header | 12 bytes | Type = `"CHAN"`, version, length, handle |
| Channel Count | 2 bytes | Number of channel records (unsigned 16-bit integer; always 1) |

Each channel record (58 bytes):

| Field | Size | Description |
|---|---|---|
| Channel ID | 4 bytes | Unique channel identifier (unsigned 32-bit integer; always 0) |
| Unknown 1 | 4 bytes | Reserved (unsigned 32-bit integer; always 0) |
| Unknown 2 | 4 bytes | Reserved (unsigned 32-bit integer; always 0) |
| Default Verbosity | 4 bytes | Default verbosity level (unsigned 32-bit integer; always 5) |
| Current Verbosity | 4 bytes | Current verbosity level (unsigned 32-bit integer; always 5) |
| Red | 1 byte | Red color component for the channel (always 255) |
| Green | 1 byte | Green color component (always 255) |
| Blue | 1 byte | Blue color component (always 255) |
| Alpha | 1 byte | Alpha component (always 255) |
| Name | 34 bytes | Channel name string, null-padded (always `"Console"`) |

Total payload size for this frame: 2 + 58 = 60 bytes.

### AINF — Application Info

**Direction**: Server → Client

Sent by the server immediately after a client connects, before `ADON` and `CHAN`.

| Section | Size | Description |
|---|---|---|
| Header | 12 bytes | Type = `"AINF"`, version, length, handle |
| Payload | 77 bytes | All zeros (placeholder / reserved for structured application information) |

This packet currently serves as a placeholder and contains no meaningful data. It may be extended in future versions to carry structured metadata about the server application.

### ADON — Additional Info

**Direction**: Server → Client

Sent by the server immediately after `AINF` during the connection handshake.

| Section | Size | Description |
|---|---|---|
| Header | 12 bytes | Type = `"ADON"`, version, length, handle |
| Unknown | 2 bytes | Reserved (unsigned 16-bit integer; always 0) |
| Name Length | 2 bytes | Length of the name string (unsigned 16-bit integer) |
| Name | Variable | Server name / identifier string (e.g., `"HLDS"`) |

This packet carries the server application name, which is a short string identifying the server type (e.g., `"HLDS"` for Half-Life Dedicated Server).



## Security

XRCON has **no built-in authentication or encryption**. There is no password validation or transport-layer security. Any client that can establish a TCP connection to the XRCON port has full command execution access to the engine console.

The only security measures are:

1. **Default localhost binding**: The server binds to `127.0.0.1` by default, accepting connections only from the local machine.
2. **Disabled by default**: The `xrcon_enable` variable defaults to `0`, so the server does not listen unless explicitly enabled.
3. **Privileged variables**: Both configuration variables are restricted to privileged users and cannot be modified through unprivileged console commands or external access.



## Comparison with Legacy RCON

| Feature | XRCON | Legacy RCON |
|---|---|---|
| Transport | TCP (stream) | UDP (datagram, out-of-band) |
| Port | Configurable, default 27000 | Game port (default 27015) |
| Framing | Binary frame header | Plain text over UDP |
| Authentication | None | Password-based |
| Max clients | 1 | Multiple |
| Interaction format | Full console access | Request-response only for submitted command |
| Supported environments | Client and server | Server only |
