## Lightmapped water

Xash3D FWGS supports lightmapped water, as an extension. It adds three new cvars and new worldspawn key values.

### For level designers:

If you're a level designer and intend to make your level to have lightmapped water, you can put these keyvalues to worldspawn entity description (always first entity in entities list):

| Key                    | Value   | Description |
| ---------------------- | ------- | ----------- |
| `_litwater`            | integer | Set to any non-zero value to enable lightmapped water. Overrides `gl_litwater_force` cvar value. |
| `_litwater_minlight`   | integer | Minimal lightmap value water surface will receive. Helps to avoid too dark areas when water isn't properly lit. If not set, defaults to zero. |
| `_litwater_scale`      | float   | Scales up lightmap value for water surfaces. If not set, defaults to 1.0. |

### For players:

Some of the maps already have computed lightmap for water surfaces and sometimes water has been properly lit but the support hasn't been declared by the level designer.

As a player, you can enable it in `Video options` menu or through console with `gl_litwater_force` cvar. There are also `gl_litwater_minlight` and `gl_litwater_scale` cvars that function similar to keys above. The default values has been set to `192` and `1.25` respectively to slightly avoid issues with maps that wasn't intended to have lightmapped water.
