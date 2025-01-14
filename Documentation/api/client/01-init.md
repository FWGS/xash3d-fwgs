# Initializing client.dll

`client.dll` has got multiple ways of initialization during it's life but we will go through the simplest and the most common route here.

Here we expect that reader has knowledge of loading dynamic shared objects on their platform. On Windows it's done through `LoadLibrary`/`GetProcAddress`/`FreeLibrary` functions, on POSIX-complaint systems it's done through `dlopen`/`dlsym`/`dlclose` functions.

## client.dll lifetime

`client.dll` is expected to be loaded during client initialization and unloaded on client shutdown. Essentially, in non-dedicated builds, it always exists during the engine lifetime. Judging by the API, it might look like `client.dll` might be safely unloaded and loaded again, for example for implementing classic `Change Game` functionality from WON versions of Half-Life, but in practice it's nearly impossible to do this clean in standard and portable manner. If you want to implement changing games, consider using `execv`-like functions.

## client.dll exported functions

The first thing you should do, is to acquire pointers to all exported functions, which you can find in the next chapter. Some of them are optional, and might not present in the `client.dll`, and will be labeled as such.

## client.dll initialization process

1. The first function you call is `Initialize` function, which lets `client.dll` to store a copy of an engine API functions.
2. Since SDK 2.0, `client.dll` have player movement code in it, which you must initialize through `HUD_PlayerMoveInit` function. `client.dll` might want to override player hulls, which can be grabbed with `HUD_GetHullBounds` function.
3. HUD functionality must be started up with `HUD_Init` function and can be de-initialized with `HUD_Shutdown` export function.
4. And finally, SDK 2.1 brings studio model renderer, which has separate set of API functions, which must be initialized with `HUD_GetStudioModelInterface` function.
