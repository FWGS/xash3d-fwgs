# Steam broker protocol
`sb_connect <ip:port> <server's steam id> <secure> <challenge>` - used for obtaining auth ticket during connection to GoldSrc server

`sb_disconnect <ip:port> <challenge>` - used for signaling broker about disconnecting from GoldSrc server

`sb_gamedir <gamedir>` - used for signaling broker about game startup and for announcing started mod, so broker could choose proper AppID for Steam API initialization

`sb_terminate` - used for signaling broker about game shutdown, ideally broker should be terminated too (and later restarted automatically and waiting for sb_gamedir message)
