# `client.dll` shared structs and enums

GoldSource exposes a lot of structs to `client.dll`. Some of them are specific to `client.dll` and only used for passing data between engine and client, some of them are used in the engine everywhere, but still exposed in the public SDK. However, many mods rely on internal structs as well, and I will try to shed light on them as well.

When implementing them in C, consider the default to 32-bit systems alignment of 4 bytes and ILP32 data type model.

This file won't have API structs, for that, there should be separate chapters.

## Enums

### Entity types enum

This enum has no real name.

| Value | Description     |
|-------|-----------------|
| `0`   | Normal entities |
| `1`   | Players         |
| `2`   | Temp entities   |
| `3`   | Beams           |
| `4`   | Fragmented entities. |

### edict->solid

It matches QuakeWorld definition.

## `client_data_t`

Used only to pass data to `client.dll` through `HUD_UpdateClientData`.

| Type     | Name          | Description                                                  |
|----------|---------------|--------------------------------------------------------------|
| `vec3_t` | `origin`      | Local client origin. Cannot be changed by `client.dll` here. |
| `vec3_t` | `viewangles`  | Local client viewangles. This and next fields can be changed by `client.dll` |
| `int`    | `iWeaponBits` | Bit vector of weapons held by client.                        |
| `float`  | `fov`         | Local client's field of view.                                |

## `netadr_t`

Matches Quake-2 structure with the similar name.

## `local_state_t`

Only consists of another structs. All of these structs are used to store prediction data, so one `local_state_t` for each predicted frame.

| Type                | Name          | Description                          |
|---------------------|---------------|--------------------------------------|
| `entity_state_t`    | `playerstate` | Contains local player's entity state |
| `clientdata_t`      | `client`      | Additional information about local client. _Do not get confused with `client_data_t`, note the underscore._ |
| `weapon_data_t[64]` | `weapondata`  | Array of 64 predictable weapons.     |

## `entity_state_t`

This structure is similar to the one that can be found in QW, but contains much more data, though most of these fields are not used by the engine, but might be used by mods.

| Type        | Name           | Description                                          |
|-------------|----------------|------------------------------------------------------|
| `int`       | `entityType`   | Entity type (see entity type enum above)             |
| `int`       | `number`       | Index of this entity on the server                   |
| `float`     | `msg_time`     | Server time at which this entity had been updated    |
| `int`       | `messagenum`   | `parsecount` at which this entity had been updated   |
| `vec3_t`    | `origin`       | Non-interpolated entity position                     |
| `vec3_t`    | `angles`       | Non-interpolated entity angles                       |
| `int`       | `modelindex`   | Server model index                                   |
| `int`       | `sequence`     | Model animation sequence                             |
| `int`       | `frame`        | Model animation frame                                |
| `int`       | `colormap`     | Texture's top and bottom colors, like Quake          |
| `short`     | `skin`         | Texture number                                       |
| `short`     | `solid`        | `edict->solid` enum                                  |
| `int`       | `effects`      | Bitmask of entity effects                            |
| `float`     | `scale`        | Entity scale value                                   |
| `byte`      | `eflags`       | |
| `int`       | `eflags`       | |
| `int`       | `eflags`       | |
| `color24`   | `eflags`       | |
| `int`       | `eflags`       | |
| `int`       | `eflags`       | |
| `float`     | `eflags`       | |
| `float`     | `eflags`       | |
| `int`       | `eflags`       | |
| `byte[4]`   | `eflags`       | |
| `byte[4]`   | `eflags`       | |
| `vec3_t`    | `eflags`       | |
| `vec3_t`    | `eflags`       | |
| `vec3_t`    | `eflags`       | |
| `vec3_t`    | `maxs`       | |
| `int`   | `eflags`       | |
| `int`   | `eflags`       | |
| `float`   | `eflags`       | |
| `float`   | `eflags`       | |
| `int`   | `eflags`       | |
| `int`   | `eflags`       | |
| `int`   | `eflags`       | |
| `qboolean` | `eflags`       | |
| `int`   | `eflags`       | |
| `int`   | `eflags`       | |
| `vec3_t`   | `eflags`       | |
| `int`   | `eflags`       | |
| `int`   | `eflags`       | |
| `int`   | `eflags`       | |
| `int`   | `eflags`       | |
| `float`    | `eflags`       | |
| `float`    | `eflags`       | |
| `int`   | `eflags`       | |
| `vec3_t`   | `eflags`       | |
| `vec3_t`   | `eflags`       | |
| `float`    | `eflags`       | |
| `float`    | `eflags`       | |
| `int`   | `iuser1`       | Few more extra fields for mods |
| `int`   | `iuser2`       | |
| `int`   | `iuser3`       | |
| `int`   | `iuser4`       | |
| `float`   | `fuser1`       | |
| `float`   | `fuser2`       | |
| `float`   | `fuser3`       | |
| `float`   | `fuser4`       | |
| `vec3_t`   | `vuser1`       | |
| `vec3_t`   | `vuser2`       | |
| `vec3_t`   | `vuser3`       | |
| `vec3_t`   | `vuser4`       | |
