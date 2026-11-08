# GameCube automatic campaign probe

The automatic campaign driver is an opt-in recording aid for GameCube builds.
Normal boots and New Game probes do not enable it.

Set `XASH_GC_PROBE_AUTOCAMPAIGN=20` when building a smoke or New Game probe disc
to add `autocampaign 20` to its `gamecube.cfg`. The value is the number of
seconds to spend on each map before the driver chooses an exit. If omitted from
the command line, the runtime uses 20 seconds.

The driver scans each loaded map's `trigger_changelevel` entities, prefers an
exit to an unvisited map, and moves the local player into that trigger so the
game's normal transition carries landmarks and entities. If the transition
does not happen within six more seconds, it queues the trigger's destination
directly and logs that fallback as `CAMPAIGN FORCED`. It logs each map arrival
and elapsed session time. A map with no changelevel trigger ends the route.

The flag can also be passed directly as `-gcautocampaign [seconds]` for a
developer build. It is intentionally absent unless selected in the boot config
or command line.
