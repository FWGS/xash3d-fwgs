# sounds.lst.md

Using sounds.lst located in scripts folder, modder can override some of the hardcoded sounds in temp entities and server physics.

File format:
```
<group name>
{
	<path1>
	<path2>
	<path3>
}

<group2 name> <path with %d> <min number> <max number>
```

* Sounds can use any supported sound format (WAV or MP3).
* The path must be relative to the sounds/ folder in the game or base directory root, addon folder, or archive root.
* Groups can be empty or omitted from the file to load no sound.
* Groups can either list a set of files or specify a format string and a range.
* Anything after // will be considered a comment and ignored.
* Behavior is undefined if the group was listed multiple times.

Currently supported groups are:
|Group name|Usage|
|----------|-----|
|`BouncePlayerShell`|Used for BOUNCE_SHELL tempentity hitsound|
|`BounceWeaponShell`|Used for BOUCNE_SHOTSHELL tempentity hitsound|
|`BounceConcrete`|Used for BOUNCE_CONCRETE tempentity hitsound|
|`BounceGlass`|Used for BOUCNE_GLASS|
|`BounceMetal`|Used for BOUNCE_METAL|
|`BounceFlesh`|Used for BOUNCE_FLESH|
|`BounceWood`|Used for BOUNCE_WOOD|
|`Ricochet`|Used for BOUNCE_SHRAP and ricochet tempentities|
|`Explode`|Used for tempentity explosions|
|`EntityWaterEnter`|Used for entity entering water|
|`EntityWaterExit`|Used for entity exiting water|
|`PlayerWaterEnter`|Used for player entering water|
|`PlayerWaterExit`|Used for player exiting water|

## Example

This example is based on defaults sounds used in Half-Life:

```
BouncePlayerShell "player/pl_shell%d.wav" 1 3
BounceWeaponShell "weapons/sshell%d.wav" 1 3
BounceConcrete "debris/concrete%d.wav" 1 3
BounceGlass "debris/glass%d.wav" 1 4
BounceMetal "debris/metal%d.wav" 1 6
BounceFlesh "debris/flesh%d.wav" 1 7
BounceWood "debris/wood%d.wav" 1 4
Ricochet "weapons/ric%d.wav" 1 5
Explode "weapons/explode%d.wav" 3 5
EntityWaterEnter "player/pl_wade%d.wav" 1 4
EntityWaterExit "player/pl_wade%d.wav" 1 4
PlayerWaterEnter
{
	"player/pl_wade1.wav"
}
PlayerWaterExit
{
	"player/pl_wade2.wav"
}
```
