### HD (external) textures support

Xash3D supports loading texture replacements in TGA format for almost all types of models in the game, except alias models at this time.

Textures are expected to be located at:
* `modfolder/materials/<mapname>` - for a specific map
* `modfolder/materials/common` - common for all maps
* `modfolder/materials/decals` - for decals
* `modfolder/materials/models/<model>` - for models (texture name must match the internal texture name in the model)

Support for high-resolution textures is enabled setting `host_allow_materials` cvar to `1` or in the menu, in "Video options" section.

#### Xash3D FWGS additions

In addition to paths above, Xash3D FWGS checks following paths:

* `modfolder/materials/sprites/<sprite>` - for sprites, except HUD sprites

Also, to check which texture replacements are loaded successfully, failed or weren't found, a mod developer can set `host_allow_materials` cvar value to `2`. The engine will spew log at any developer level in the following format:

```
Looking for <replacement> replacement... <status code> (<path relative to mod directory>)
```

Status codes:
* `OK` - texture replacement file was found and loaded into GPU memory successfully
* `FAIL` - texture file was found but hasn't been parsed or loaded successfully. Refer to engine log for more details.
* `MISS` - texture file wasn't found

Example:
```
Looking for maps/bounce.bsp:!waterblue tex replacement...OK (materials/common/!waterblue.tga)
Looking for maps/bounce.bsp:!waterblue_luma tex replacement...MISS (not found)
Looking for {shot2 decal replacement...MISS (materials/decals/{shot2.tga)
Looking for {shot4 decal replacement...MISS (materials/decals/{shot4.tga)
Looking for {shot3 decal replacement...MISS (materials/decals/{shot3.tga)
Looking for models/gman tex replacement...FAIL (materials/models/gman/GMan_Case1.tga)
Looking for models/gman tex replacement...FAIL (materials/models/gman/inside_1.tga)
```

