# Steam API Broker Protocol Specification

## Overview

Due to proprietary nature of Steamworks SDK, it cannot be run on same amount
of platforms supported by Xash3D FWGS, neither we can link library directly due to
GNU GPLv3 license. 

However, here comes the broker, by running it (in trusted
network, preferrably) on a machine that has Steam client installed, the
engine can communicate with it, acquiring needed information to log-in into
Steam protected multiplayer servers.

## Key Characteristics

Broker uses a simple TCP-based binary protocol with the following properties:

- Single active session per connection: once a session is activated, the broker will reject all other TCP connections until the session is terminated.
- All messages follow a consistent structure with a header, length, and payload: this is called a "frame".
- Stateful interactions: commands affect the session state, and certain commands are only valid in specific states.
- Numeric parameters are encoded in little-endian format for compatibility with majority of platforms.
---

## Frame Format

All communication uses a fixed frame structure:

| Field | Size | Type | Description |
|-------|------|------|-------------|
| Header | 4 bytes | ASCII | Frame start signature |
| Length | 2 bytes | uint16_t (LE) | Size of payload in bytes |
| Payload | N bytes | Binary | Command string or response data |

**Notes**:
- Header must be exactly `SBRK` (`53 42 52 4B` in hexadecimal form)
- Maximum frame size: soft limit is 4096 bytes, hard limit is 65535 bytes (due to uint16_t usage)

---

## Session Lifecycle

```
┌──────────────┐
│     IDLE     │ ← Initial state, accepts new TCP connections
└──────────────┘
       │ sb_gamedir
       ↓
┌──────────────┐
│    ACTIVE    │ ← Session established, rejecting all of other TCP connections
└──────────────┘
       │ sb_connect
       ↓
┌──────────────┐
│    ACTIVE    │ ← Client getting auth ticket to connect to game server
└──────────────┘
       │ sb_disconnect
       ↓
┌──────────────┐
│     ...      │ ← Client announcing disconnect from game server, could connect somewhere again
└──────────────┘
       │ 
       ↓
┌──────────────┐
│    ACTIVE    │ ← Client shutting down game, terminating broker to reset Steam client state
└──────────────┘
       │ sb_terminate
       ↓
┌──────────────┐
│  RESTARTING  │
└──────────────┘
```

---

## Commands

All commands listed below are meant to be sent from client to broker as payload of a frame. The broker will parse the command string and respond accordingly.

### Session activation

Activates the session and signaling game startup.

**Format**: `sb_gamedir <gamedir>`

**Response**: None

---

### Authentication ticket request

Requests authentication ticket for a game server.

**Precondition**: Session must be active

**Format**: `sb_connect <ip:port> <server_steamid> <secure> <challenge>`

**Parameters**:

| Name | Description |
|------|------------------|
| `<ip:port>` | Game server address (e.g., `127.0.0.1:27015`) |
| `<server_steamid>` | Server SteamID |
| `<secure>` | 0 = insecure, 1 = secure |
| `<challenge>` | Random value for reply prevention |


**Response Format**:

| Field | Size | Type | Description |
|-------|------|------|-------------|
| Header | 11 bytes | string | `"sb_connect\n"` |
| Challenge | 4 bytes | int32_t (LE) | Echo of challenge from request |
| SteamID | 8 bytes | uint64_t (LE) | Client SteamID from configuration |
| Size | 4 bytes | uint32_t (LE) | Length of ticket in bytes |
| Ticket | N bytes | - | Authentication ticket data |

---

### Server disconnection announcement

Notifies broker of disconnection from game server.

**Precondition**: Session must be active

**Format**: `sb_disconnect <ip:port> <challenge>`

**Response**: None

---

### Session termination

Signaling game shutdown to broker and resetting Steam client state.

**Format**: `sb_terminate`

**Response**: None

---
