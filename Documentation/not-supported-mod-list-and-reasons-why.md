# Not supported mods and reasons why

|Name							|Version			|Why not working						|What was made for that
|----							|-------			|---------------						|----------------------
|Area 51						|Update 1			|Uses outdated BSP31 map format and custom HLFX SDK libraries.	|You can try [this tool](https://hlfx.ru/forum/showthread.php?threadid=5250) to convert maps but there no warranty if it works.
|Arrange Mod: Rebirth					|v150				|No idea yet.							|
|Blue Shift						|The latest steam release	|Uses vgui2 library which xash3d does not support.		|Recreated source code here: https://github.com/FWGS/hlsdk-xash3d/tree/bshift.
|Counter Strike						|Beta 6.5-			|Uses an old WON HL 1.0.0.16- interface.			|
|							|1.4				|Has encrypted blob instead of normal client.dll.		|You can try [this tool](https://aluigi.altervista.org/papers/hldlldec.zip) to decrypt client.dll but there no warranty if it works.
|							|1.5				|Has encrypted blob instead of normal client.dll.		|Decrypted blob here: https://csm.dev/threads/cs-1-5-client-dll-decrypted-patched-for-usage.38845.
|							|1.6(The latest steam release)	|Uses vgui2 library which xash3d does not support.		|Some work on vgui2 support was made here: https://github.com/FWGS/xash3d/tree/vinterface. Recreated Client Source Code here: https://github.com/Velaron/cs16-client.
|Counter Strike: Condition Zero				|The latest steam release	|Uses vgui2 library which xash3d does not support.		|Some work on vgui2 support was made here: https://github.com/FWGS/xash3d/tree/vinterface. Recreated Client Source Code here: https://github.com/Velaron/cs16-client.
|Counter Strike: Condition Zero - Deleted scenes	|The latest steam release	|Uses vgui2 library which xash3d does not support. Uses new sequences code on engine-side that was never used in any other mods before.		|Some work on vgui2 support was made here: https://github.com/FWGS/xash3d/tree/vinterface.
|Day of Defeat						|The latest steam release	|Uses vgui2 library which xash3d does not support.		|Some work on vgui2 support was made here: https://github.com/FWGS/xash3d/tree/vinterface.
|Half-Life: Extended					|Day One demo			|Uses many hooks to GoldSource engine and version check.	|Just wait new version or use more old version.
|Icon of Hell						|Beta 0.99			|Uses outdated BSP31 map format and paranoia 2 libraries.	|You can try [this tool](https://hlfx.ru/forum/showthread.php?threadid=5250) to convert maps and use Paranoia 2: The Savior 1.51 libraries but there no warranty if it works.
|Paranoia 2: The Savior					|All builds older 1.51		|Uses an old renderer interface and engine features.		|
|Rebellion						|1.0				|Uses an old WON HL 1.0.0.16- interface.			|Recreated source code here: https://github.com/FWGS/hlsdk-xash3d/tree/rebellion.
|Sven-Coop						|5.0+				|Uses custom GoldSrc engine.					|
|Time Shadows						|Beta 0.1			|Uses Direct3D renderer.					|
