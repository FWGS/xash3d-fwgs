# Xash3D 49 connectionless protocol

Connectionless protocol defines four common message sources and destinations, they can be:
* `S` for Server
* `C` for Client
* `M` for Master
* `A` for Anything else

All connectionless packets have `\xff\xff\xff\xff` (32-bit integer set to all ones) in it's header. Let's go through each of possible direction in this document.

## Any

### Any to Any

#### `A2A_NETINFO`

Used to implement Half-Life's NetAPI. 
* Request message, in ASCII: `netinfo <version> <context> <request_id>`
  - `version` must be 49, as text.
  - `context` can be any 32-bit signed integer value, encoded as text.
  - `request_id` see `common/net_api.h`

* Response message, in ASCII: `netinfo <context> <request_id> <response>`
  - `context` same as in request.
  - `request_id` same as in request.
  - `response` is server's response, depending on request type. Always encoded as Quake info string.

Possible requests and response formats:
* Request ID `1` will make server respond with empty response string. 
* Request ID `2` makes server respond with all server game rule cvars where the key is cvar name and the value is cvar value. One additional key `rules` with total amount of cvars is added.
* Request ID `3` makes server respond with players list, where:
  - `p<id>name` is set to player's name
  - `p<id>frags` is set to player's kill count
  - `p<id>time` is set to player's total play time
  - `players` is total player's list.
  - Alternatively, it can respond with `neterror` set to `forbidden` if server does not wish to expose it's player list.
* Request ID `4` makes server to respond with common game details:
  - `hostname` is server's name
  - `gamedir` is game directory name
  - `current` is total player count
  - `max` is max players limit
  - `map` is server's current level
* On any other server will respond with `neterror` set to `undefined`
* If protocol version doesn't match, server responds with `neterror` set to `protocol`

#### `A2A_INFO`

Used to request server's details.
* Request message, in ASCII: `info <version>`
  - `version` must be 49, as text
* Response message, in ASCII: `info\n<response>`
  - `response` is Quake info string, otherwise it's an error message.

* Response info string format:
  - `p` is set to protocol version (FWGS extension)
  - `map` is server's current level
  - `dm` set to 1 if current game mode is deathmatch otherwise 0
  - `team` set to 1 if current game mode includes teamplay otherwise 0
  - `coop` set to 1 if current game mode is cooperative otherwise 0
  - `numcl` is total player count
  - `maxcl` is max players limit
  - `gamedir` is game directory name
  - `password` set to 1 if server is protected with password otherwise 0
  - `host` is server's name

#### `A2A_PING` and `A2A_GOLDSRC_PING`

Simple ping message.
* Request, in ASCII: `ping` or `i` in GoldSrc format
* Response is always `A2A_ACK` or `A2A_GOLDSRC_ACK`

#### `A2A_ACK` and `A2A_GOLDSRC_ACK`

Simple ack message.
* Request, in ASCII: `ack` or `j` in GoldSrc format
* Response: none

### Any to Client

#### `A2C_PRINT` and `A2C_GOLDSRC_PRINT`

Simple print message.
* Header: `\xff\xff\xff\xff`
* Request, in ASCII: `print <message>` or `l<message>` in GoldSrc format.
* Response: none

### Any to Server and Server to Any

#### `A2S_GOLDSRC_INFO`, `A2S_GOLDSRC_RULES` and `A2S_GOLDSRC_PLAYERS` and their responses

These match Source Engine Query messages used in GoldSrc and Source engine and implemented for compatibility with existing tools. Read documentation for them on VDC: https://developer.valvesoftware.com/wiki/Server_queries

### Any to Master

#### `S2M_SCAN_REQUEST`

* Request: `1<region><IP:Port>\0<info>`
  - Format of this message is loosely based on https://developer.valvesoftware.com/wiki/Master_Server_Query_Protocol
  - `info` however adds few additional fields:
    - `clver` set to engine's version
    - `nat` set to 1 to filter only servers behind NAT and tell master to notify servers about scan
    - `commit` set to engine's build commit hash
    - `branch` set to engine's build branch
    - `os` set to which operating system engine has been built
    - `arch` set to which CPU architecture engine has been built
    - `buildnum` set to engine's build number
    - `key` set to random 32-bit value formatted as hex, used to validate master responses
* Response: `M2A_SERVERSLIST`

### Master to Any

#### `M2A_SERVERSLIST`

* Request: `f\xff<key><reserved><IP:Port>...`
  - `key` is random 32-bit value set in `S2M_SCAN_REQUEST`
  - `reserved` is single reserved 8-bit byte
  - Following is list of IP addresses in binary form: 6-bytes for IPv4 address + port and 18-bytes for IPv6 address + port
  - If port is 0, it means the end of list, otherwise it's the last IP used for pagination (not implemented in Xash3D)
* Response: client doesn't make response to master server but might request `A2S_INFO` from servers

## Server to Master

### `S2M_HEARTBEAT`

Used to update game server information on the master server.

* Request: `q\xff<heartbeat challenge>`
  - `heartbeat challenge` is random 32-bit value, used to prevent faked/forged source IP addresses.
* Response: `M2S_CHALLENGE`

### `S2M_SHUTDOWN`

Used to notify master server about server shutdown, but due to security reasons must be ignored by any master implementation.

* Request: `\x62\x0a`
* Response: none

### `S2M_INFO`

Game server info response on `M2S_CHALLENGE`.

* Request: `0\n<info>`
  - `info` is Quake info string containing server information passed to master server. It contains following fields:
    - `protocol` is always 49
    - `challenge` is master challenge
    - `players` is total player count, without bots
    - `max` is max players limits
    - `bots` is total bot count
    - `gamedir` is set to game directory
    - `map` is server's current level
    - `type` set to `d` for dedicated or `l` for listen server
    - `password` set to `1` if server protected with password set otherwise `0`
    - `os` always `w`
    - `secure` always `0`
    - `lan` always `0`
    - `version` engine version
    - `region` always `255`
    - `product` same as `gamedir`
    - `nat` set to `1` if server is behind NAT
* Response: none

## Master to Server

### `M2S_CHALLENGE`

Master's respoonse on game server's heartbeat message.

* Request: `s<master challenge><heartbeat challenge>`
  - `master challenge` contains 32-bit value, used in server response to prevent faked/forged source IP addresses.
  - `heartbat challenge` cotnains 32-bit value, used in heartbeat request to prevent faked/forged source IP addresses.
* Response: `S2M_INFO`.

### `M2S_NAT_CONNECT`

Master server's message with client IP address and port, used in NAT punching.

* Request: `c <IP:Port>`
* Response: `S2C_INFO` to a specified client address.

## Client to Server

### `C2S_BANDWIDTHTEST`

Used to figure out network MTU. The message is optional and server might choose to not implement it or respond with challenge.

* Request, in ASCII: `bandwidth <version> <max_size>`
  - `version` must be 49, as text.
  - `max_size` is requested maximum packet size.

* Possible responses:
  - `S2C_BANDWIDTHTEST`
  - `S2C_CHALLENGE`
  - `A2A_PRINT` followed by `S2C_REJECT`
  - `S2C_ERRORMSG` (as FWGS extension) followed by `A2A_PRINT` and `S2C_REJECT`.

### `C2S_GETCHALLENGE`

Used to validate client's address to prevent faked/forged source IP addresses.

* Header: `\xff\xff\xff\xff`
* Request, in ASCII: `getchallenge steam`
  - `steam` argument is optional and can be ignored by the server, as it's only kept for compatibility with similar GoldSrc 48 message.

* Possible responses:
  - `S2C_CHALLENGE`

### `C2S_CONNECT`

Used to build a connection with the server.

* Header: `\xff\xff\xff\xff`
* Request, in ASCII: `connect <version> <challenge> "<protinfo>" "<userinfo>"`
  - `version` must be 49, as text.
  - `challenge` must be challenge value, as text.
  - `protinfo` is a Quake info string, containing protocol extensions and other connection information.
    - `uuid` is this client's unique ID (FWGS extension) hasheed with MD5.
    - `qport` is random integer value from 1 to 65535, unique for this engine run.
    - `ext` is an integer value OR'ed with requested protocol extensions. Currently only extension is `NET_EXT_SPLITSIZE` with the value of `1`, which tells server to split messages based on `cl_dlmax` value from `userinfo`. 
  - `userinfo` contains initial user info, encoded as Quake info string.
* Possible responses:
  - `A2A_PRINT` followed by `S2C_REJECT`
  - `S2C_ERRORMSG` (as FWGS extension) followed by `A2A_PRINT` and `S2C_REJECT`.
  - `S2C_CONNECTION`

## Server to Client

### `S2C_BANDWIDTHTEST`

* Request: `testpacket<crc><blob>`
  - `crc` is 32-bit CRC of `blob`
  - `blob` random data
* There is no required response to this message

Total message size doesn't exceed request max size.

### `S2C_CHALLENGE`

* Request, in ASCII: `challenge <value>`
  - `value` is challenge value that client must include in it's response
* Response: `C2S_CONNECT`

### `S2C_CONNECTION`

* Request, in ASCII: `client_connect <protinfo>`
  - `protinfo` is an optional Quake info string, contains following optional fields:
     - `ext` is an integer value OR'ed with allowed protocol extension. Bit fields match requested in `ext` field in `C2S_CONNECT`
     - `cheats` set to 1 if server allows cheats otherwise 0
* Response: there is no out of band from the client but client will proceed to building netchan and switch client-server interaction to it.

### `S2C_ERRORMSG`

Show client error message. Doesn't mean connection reject, only used for UI.

* Request, in ASCII: `errormsg <message>`
  - `message` contains error message
* Response: none

### `S2C_REJECT`

Client has been rejected in connection.

* Request, in ASCII: `disconnect`
* You shouldn't respond to this message but you might cope, get depressed or mentally tell server to fuck off.
