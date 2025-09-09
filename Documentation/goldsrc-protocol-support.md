# Support for GoldSrc network protocol
This feature is still work-in-progress, but for now it's available for all users, and we appreciate any bug-reports and contributions around it.

For connecting to GoldSrc-based servers, use this command:
```
connect ip:port gs
```

But keep in mind, there are requirement for server to be able to accept connections from Xash3D-based clients: it should use Reunion.
Without this requirement, you will just get "Steam validation rejected" error on connecting.

That is because proper authorization with Steam API is not implemented in engine yet (but we have plans on it).

Also, we encountered that some GoldSrc-based servers are recognizing Xash3D clients as "fake clients" and banning/kicking them. Maybe this problem will be
solved along with better compatibility with GoldSrc behavior, but may be not - we don't know logic behind this fake client checks.
