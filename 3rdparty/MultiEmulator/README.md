# MultiEmulator

**MultiEmulator** - project for GoldSource Engine, which provides ability to generate tickets with a specific SteamID key, using all available emulators for this engine.

Ticket is processed by the game server of Half-Life 1 and modifications using **DProto** or **ReUnion** modules. If the server does not have these modules, then MultiEmulator won't work.

# Ticket generators

Currently available ticket generators: 
* OldRevEmu
* AVSMP
* Setti
* SteamEmu
* RevEmu
* SC2009
* RevEmu2013

Currently under development generators:
* SmartSteamEmu (SSE3)

# How to use?
Each file in the **MultiEmulator\Source\Emulators** folder contains a **Generate** function that writes a ticket to the **pDest** argument for the emulator of the same name as a header file. As a result of the function, the size of the written ticket is returned. If the generator can set an arbitrary SteamID, the function will have the **nSteamID** argument, in which the required SteamID is specified.

# Examples
As an example, the DLL was developed that searches InitiateGameConnection method in ISteamUser interface, which generates Steam ticket, and inserts in its place own generator, which creates RevEmu2013 ticket with SteamID equal to 3333333. You can find this project in **Example** folder, you just need to compile it and perform DLL injection in hl.exe process with any known injection method.