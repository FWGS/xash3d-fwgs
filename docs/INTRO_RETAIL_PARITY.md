# Intro tram ride: retail parity

What the GameCube build does on the Half-Life intro (menu New Game,
c0a0 → c0a0e) compared with the retail PC game, checked in Dolphin from
recorded playtests (`.ai/playtests/`, see docs/GAMECUBE_PLAY_DOLPHIN.md)
and the engine log. Retail behaviour is taken from the maps' own entities
(trigger_auto / multi_manager timings, ambient_generic, env_message) and
the stock game code.

Reference recording: `.ai/playtests/20261030-015618-menu-newgame-d1c92d7269.mp4`
(full ride, 429 s, kept), probe log `.ai/logs/dolphin-probe-20261030-015615`.

## Verified

| Area | Retail | GameCube build | Evidence |
|---|---|---|---|
| Tram across changelevels | The tram is one global entity (`globalname intro_train`) carried through every landmark transition with the player riding it | Same: the global merges into each map's copy at every hop (c0a0→a→b→c→d→e), keeps its speed, and the player is back on it (`groundentity` = tram) within 3 s | `hop check` lines: before / after transition / +3 s / +10 s for all 5 hops |
| Announcer (VO) | ambient_generic lines fired by each map: gmorn, time (c0a0), dest (a), noeat, emerg (b), tourn, jobs (c), haz, arrive (d), exit (e) | All 10 play in full, once, at the time the map fires them. They are streamed from the disc (250–840 KB each, too big for the sound cache) | `VO stream media/c0a0_tr_*.wav ok=1`, one per line, in order |
| c0a0 VO timing | trigger_auto 1 s → audiomm: gmorn at ~2 s, time at ~15 s (no overlap: gmorn is 11.6 s) | Same, from the map's own entities (was: a hand schedule that started "time" 3 s into "good morning") | `Firing: (wav1)` / `(wav2)` 13 s apart |
| Tram sounds | func_tracktrain `sounds 1`: plats/ttrain_start1, ttrain1 (loop, pitch by speed), ttrain_brake1 | Same sounds, from the tram entity (the old background rumble loop that stood in for them is gone) | `registered late plats/ttrain*.wav`, no budget refusals |
| Other game sounds | NPC footsteps, doors, sparks, buttons | Play (they were all dropped before: the client of this route never got the sound list) | `registered late common/npc_step1.wav` etc. |
| Credits / titles | env_message CR1–CR38, CR27, OPENTITLE3/4, the c0a0c tube panels CR28–CR34 | All shown with their titles.txt effects (fade, flicker, write-out) | 40 fired, 40 `HudText … displayed`, none `done frames=0` (half were dropped before) |
| Credit timing (c0a0) | start_titles 0.3 s → title_manager +6 → CR1..CR8 every 3 s | CR1..CR8 3.0 s apart | `HudText CRn effect=… at t` |
| Changelevel | GoldSrc keeps the last frame with a LOADING plaque while the next map loads | Last frame held with "LOADING" and animated dots; same on the first New Game load | `loading overlay on/off`, recording frames at each hop |
| A/V sync | — | Picture freeze and sound stop at each hop line up with the engine log within ~0.1 s | Recording vs log, docs/GAMECUBE_PLAY_DOLPHIN.md |

## Open

| Area | Retail | GameCube build now |
|---|---|---|
| Sentences | Scientists / Barney speak `!` sentences | Silent: SENTENCEG_Init is skipped on the GameCube game code |
| Light styles | Flickering / switched lights animate | Static (lightmaps are baked once) |
| Hold length | A few frames on PC | 2–5 s per hop (disc reads); covered by the LOADING overlay |
| NPC start-up | Smooth | Frame time is high for the first ~15 s of c0a0 while NPC AI starts |
| c0a0e exit | Door opens, the player walks out to c1a0 | Ride and arrival verified; leaving the tram and the c0a0e → c1a0 hop not yet checked |
| Boot | Menu after the logos | A garbled status panel can show briefly before the menu |
| c0a0a | Smooth ride | Occasional judder |
