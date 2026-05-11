# Xash3D FWGS protocol support

Xash3D FWGS currently supports four network protocols:
* Version 49, introduced in Xash3D in around 2018.
* Version 48, used in Xash3D FWGS 0.19 and earlier. Deprecated and to be removed in the future.
* GoldSrc version 48, used in current GoldSource version.
* Quake version 15, only used for demo playback for Quake Wrapper mod.

On the server side, we only support Xash3D 49 protocol, but bugcomp `gsmrf` mode can convert GoldSrc 48 messages into Xash3D 49 on the fly, for some mods that directly write engine internal messages.

In the following documents we will only cover version 49, as everything else is either not Xash3D specific or deprecated.
