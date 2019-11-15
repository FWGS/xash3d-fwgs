## Console variables

Console variables (or CVars) are present in all quake-based games.

By default, it is settings, created by engine, server or client libraries.

But you can use `set` command to define variables even if they are not created by the engine.

For example, you can set cvar before it is registered in code.

`set defaultmap crossfire`

This works even in server.cfg before server cvars initialization and the engine will reuse its value on cvar creation

## Aliases

An alias allows to define new commands.

`alias wnext "invnext;wait;wait;+attack;wait;-attack"`

You can hook any command by adding an alias to it and unaliasing it, when you want to use original command.

```
alias invnext1 "unalias invnext;wnext;alias invnext invnext1"
alias invnext invnext1
```

## Scripting extensions

This is an extensions of Xash3D FWGS(merged to original Xash3D since build 3887), that can be enabled by cmd_scripting cvar.

Enabling scripting: `cmd_scripting 1`

This is an archive cvar and it will be saved.

### CVar substitution

You can substitute cvar value to any command by adding \$ symbol:

`echo $sv_cheats`

### Condition checking

Allows checking cvar values.

```
if <value1> <operator> <value2>
:<action1>
:if <value3>
::<action2>
:<action3>
else
:<action4>
```

* Values are any string or numeric values (for example, substituted cvars).
* Operator is = (or ==), \!=, \<, \>, \<=, \>=. == is same to =.
* If single value specified, condition is true when value is non-zero

Example:

```
if $sv_cheats == 1
:echo Cheats enabled, adding cheat menu
:exec cheatmenu.cfg
else
:echo Please enable cheats to use this!
```
