# Support for GoldSrc network protocol
This feature is still work-in-progress, but for now it's available for all users, and we appreciate any bug-reports and contributions around it.

For connecting to GoldSrc-based servers, use this command:
```
connect ip:port gs
```

But keep in mind, there are requirement for server to be able to accept connections from Xash3D-based clients: it should accept HLTV connections.
Without this requirement, you will just get "Steam validation rejected" error on connecting.

That is because proper authorization with Steam API is not implemented in engine yet (but we have plans on it).

In case of ReHLDS with Reunion plugin, by default it rejects HLTV clients. But connections from HLTV can be easily enabled in `reunion.cfg` file.
