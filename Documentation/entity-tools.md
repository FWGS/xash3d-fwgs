# There are few new commands availiable in xash3d fork:

## Commands:
### ent_create
Create entity with specified classname and key/values

`ent_create <classname> <key> <value> <key> <value> ...`

for example:

`ent_create monster_zombie targetname zomb1`

after creating entity, ent_last_xxx cvars are set to new entity and ent_last_cb called, look at ent_getvars description

### ent_fire

Make some actions on entity

`ent_fire <pattern> <command> <args>`
Availiavle commands:
* Set fields (Only set entity field, does not call any functions):
   * health
   * gravity
   * movetype
   * solid
   * rendermode
   * rendercolor (vector)
   * renderfx
   * renderamt
   * hullmin (vector)
   * hullmax (vector)
* Actions
   * rename: set entity targetname
   * settarget: set entity target (only targetnames)
   * setmodel: set entity model (does not update)
   * set: set key/value by server library
       * See game FGD to get list.
       * command takes two arguments
   * touch: touch entity by current player.
   * use: use entity by current player.
   * movehere: place entity in player fov.
   * drop2floor: place entity to nearest floor surface
   * moveup: move entity to 25 units up
   * moveup (value): move by y axis relatively to specified value
* Flags (Set/clear specified flag bit, arg is bit number):
   * setflag
   * clearflag
   * setspawnflag
   * clearspawnflag

### ent_info
Print information about entity by identificator

`ent_info <identificator>`

### ent_getvars
Set client cvars containing entity information (useful for [[Scripting]]) and call ent_last_cb

`ent_getvars <identificator>`

These cvars are set:
```
ent_last_name
ent_last_num
ent_last_inst
ent_last_origin
ent_last_class
```

### ent_list
Print short information about antities, filtered by pattern

`ent_list <pattern>`

## Syntax description

### \<identificator\>

* !cross: entity under aim
* Instance code: !\<number\>_\<seria\l>
 * set by ent_getvars command
* Entity index
* targetname pattern

### \<pattern\>

Pattern is like identificator, but may filter many entities by classname

### (vector)

used by ent_fire command. vector means three float values, entered without quotes

### key/value

All entities parameters may be set by specifiing key and value strings.

Originally, this mechanizm is used in map/bsp format, but it can be used in enttools too.

Keys and values are passed to server library and processed by entity keyvalue function, setting edict and entity owns parameters.

If value contains spaces, it must be put in quotes:

`ent_fire !cross set origin "0 0 0"`

## Using with scripting

ent_create and ent_getvars commands are setting cvars on client

It can be used with ent_last_cb alias that is executed after setting cvars.

Simple example:

```
ent_create weapon_c4
alias ent_last_cb "ent_fire \$ent_last_inst use"
```

Use weapon_c4 after creating it.

Note that you cannot use many dfferent callbacks at the same time.

You can set entity name by by pattern and create special script, contatning all callbacks.

Example:

example.cfg
```
alias ent_last_cb exec entity_cb.cfg
ent create \<class\> targetname my_ent1_$name
ent_create \<class\> targetname my_ent2_$name
```
entity_cb.cfg
```
if $ent_last_name == my_ent1_$name
:(ent1 actions)
if $ent_last_name == my_ent2_$name
:(ent2 actions)
```
Note that scripting cannot be blocking. You cannot wait for server answer and continue. But you can use small scripts, connected with ent_last_cb command. The best usage is user interaction. You can add touch buttons to screen or call user command menu actions by callbacks.
##  Server side

To enable entity tools on server, set sv_enttools_enable to 1

To change maximum number of entities, touched by ent_fire, change sv_enttools_maxfire to required number.

To enable actions on players, set sv_enttools_players to 1.

To enable entity tools for player by nickname, set sv_enttools_godplayer to nickname. Useful to temporary enable from rcon.

To prevent crash on some actions, set host_mapdesign_fatal to 0