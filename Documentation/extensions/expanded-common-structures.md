# Expanded structures that used by engine and mods
To make porting and developing mods on 64-bit platforms less painful, we decided to expand size of several structures.
This information important in case you are using codebase like XashXT, Paranoia 2: Savior and want to compile your mod for platform with 64-bit pointer size: you should replace old definitions with new ones, otherwise your mod will not work with Xash3D FWGS (typically, it's just crashing when starting map).
| Structure name | Locates in file | Original size on 64-bit | Current size on 64-bit |
|----------------|-----------------|-------------------------|------------------------|
|`mfaceinfo_t` | `common/com_model.h` | 176 bytes |  304 bytes |
|`decal_s` | `common/com_model.h` | 72 bytes |  88 bytes |
|`mextrasurf_t` | `common/com_model.h` | 376 bytes |  504 bytes |
