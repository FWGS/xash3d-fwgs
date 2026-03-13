# Support for GoldSrc network protocol
For connecting to GoldSrc-based servers, use this command:
```
connect ip:port gs
```

You need to use [Steam API broker](https://github.com/FWGS/steam-broker) and have purchased Half-Life 1 on your Steam account to be able to join GoldSrc servers.

Broker will run on your PC and will be responsible for communicating with Steam client and obtaining auth tickets.
Also, make sure to set that broker as current ticket generator using console or edit `config.cfg` file manually:
```
cl_ticket_generator steam
```

If you want to join GoldSrc servers from other device (Android/iOS or console), make sure to set proper broker IP address with `cl_steam_broker_addr` console variable. Default value assumed that broker is running on the same device as game client.

By the way, we encountered that some GoldSrc-based servers are recognizing Xash3D clients as "fake clients" and banning/kicking them. Maybe this problem will be
solved along with better compatibility with GoldSrc behavior, but may be not - we don't know logic behind this fake client checks.
