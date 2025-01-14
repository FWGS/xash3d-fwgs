# What is client.dll in GoldSource?

One of technical differences a programmer working with Quake engine might notice is the client.dll file. Historically, it appeared somewhere in between Alpha 0.52 and NetTest1 development and judging by Half-Life SDK 1.0 was meant to handle HUD rendering. The time moved forward, SDK 2.0 finalized `client.dll` API at version 7, added an in-game UI, custom input processing, player movement and weapon prediction, very basic rendering through TriAPI. SDK 2.1 added ability to re-define studio model rendering, and so on.

It somewhat resembles `cgame` module from Quake 3, but more crudely designed and sometimes feels like an afterthought, considering how many engine internal structures it exposes and the main point of incompatibilities with mods, which these days sometimes use `client.dll` as a way to inject custom rendering into the game.

In this document I will try to go through each step, letting you, dear reader, implement your own GoldSrc compatible API in your Quake fork, targetting vanilla Half-Life `client.dll` from latest update, which at the time of writing, is 25-th anniversary update.

Despite that we call it `client.dll`, since SDK 2.4 (unofficial naming, it's the first SDK Valve published on GitHub) it is considered portable and only has SDL2 and VGUI libraries in it's external dependences. It's only called this way to avoid possible misunderstandings with engine developers, who might interpret client as `cl_` prefixed part of Quake engine.
