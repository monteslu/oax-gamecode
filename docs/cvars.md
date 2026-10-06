# oax game cvars and test commands

The cvars the oax game modules (qagame, cgame) register on top of the
OpenArena ones. Defaults and flags are as registered in the code. Most
features also need the matching engine feature token in `oax_features`;
on an engine without it the cvar has no effect.

Flags: `serverinfo` (sent to clients), `latch` (takes effect on the next map),
`archive` (saved to the config), `cheat` (needs `sv_cheats 1` / `devmap`).

## Server (qagame)

| Cvar | Default | Flags | What it does |
| --- | --- | --- | --- |
| `g_oaxVehicles` | 0 | serverinfo, latch | Vehicle rule. 1 spawns vehicles from `info_oax_vehicle` spawners (types `buggy`, `hover`, `apc` with a roof gunner, `hovertank`); 0 frees the spawners and classic play is untouched. |
| `g_oaxTranslocator` | 0 | serverinfo, latch | Translocator rule. 1 gives every player a translocator in the grappling hook's slot. |
| `g_oaxScripts` | 1 | | 0 never starts the map script VM (for cost comparisons). |
| `g_scriptSeed` | 0 | | Seed (mixed with the map name) for the random numbers map scripts draw. |
| `g_oaxAssaultTime` | 0 | | Assault (`g_gametype 14`, see [assault.md](assault.md)): the round's time limit in seconds; 0 uses the map's. |
| `g_oaxAssaultFirst` | 0 | archive | Assault: 0 red attacks first, 1 blue. |
| `g_oaxVehSolid` | 1 | cheat | 1: players collide with vehicles' oriented boxes (stand on decks, get pushed). 0: vehicles are not solid to players (shots still hit them). |
| `g_oaxVehLog` | 0 | | 1 prints a `vehlog` line per vehicle physics tick; 2 also prints each driver command (`vehcmd`). |
| `g_oaxLandFix` | 1 | cheat | 1: a player landing with its velocity straight into the ground keeps no speed (fixes an endless landing bounce). 0 restores the stock landing, as a test control. Only the server's pmove reads it: client prediction always uses the fix, so with 0 the client mispredicts landings. That is intended for the control. |
| `bot_oaxNav` | 1 | | Navigation-mesh bots (maps without AAS). 0 is a test control: bots steer straight at their goal with no navmesh path and no stuck recovery. |

## Client (cgame)

| Cvar | Default | Flags | What it does |
| --- | --- | --- | --- |
| `cg_physics` | 1 | archive | Cosmetic physics (gibs, brass, explosion push, debris). 0 runs the stock fragment code. |
| `cg_physDebris` | 1 | archive | 0 keeps the explosion push but throws no debris chunks. |
| `cg_physWorkers` | 1 | archive | Worker thread count of the cosmetic physics world. |
| `cg_oaxParticles` | 1 | archive | Particle systems for weapon impacts and `func_oax_emitter`. |
| `cg_oaxDecals` | 1 | archive | Projected decals for lasting impact marks (0: stock mark polygons). |
| `cg_oaxTrails` | 1 | archive | Ribbon trails behind rockets, grenades, plasma and entities with an `oaxtrail` key. |
| `cg_oaxVehPredict` | 1 | archive | Predict your own vehicle locally (the server stays authoritative). |
| `cg_oaxVehErrorDecay` | 150 | archive | Milliseconds over which a vehicle prediction error is blended out. |
| `cg_oaxVehSnap` | 96 | archive | A prediction error over this many units snaps instead of blending (a teleport). |
| `cg_oaxVehHud` | 1 | archive | The vehicle HUD: vehicle health for riders, speed for the driver, heat or reload for whoever has the gun. |
| `cg_oaxVehView` | 0 | archive | The view from a vehicle seat: 0 by seat (cockpit drivers and gunners first person, drivers of open vehicles the chase camera), 1 first person, 2 the chase camera. `toggleview` switches it. |
| `cg_oaxAssaultHud` | 1 | archive | The Assault HUD: role, clock, objectives, markers over the open ones. |
| `cg_oaxVehHints` | 1 | archive | A line of the vehicle controls, shown for a few seconds after taking a seat. |
| `cg_oaxVehLookReturn` | 800 | archive | Milliseconds without look input before a seat without the gun swings its view back behind the vehicle. |
| `cg_oaxGroundFx` | 1 | archive | Ground effects on maps whose worldspawn sets `oax_groundfx 1`: rings where players wade, vehicle dust, splashes and tyre tracks. |
| `cg_oaxUnderwaterFog` | "" | archive | Overrides the view fog (`r g b density`) while the eye is in water, on maps whose worldspawn sets `oax_underwaterfog`; empty = the map's. |

## Test-only cvars

These exist for automated tests and controls; players do not need them.

| Cvar | Default | Flags | What it does |
| --- | --- | --- | --- |
| `g_oaxVehNoTerrain` | (unset) | | Not registered; read when the vehicle world is built. 1: vehicles get no terrain collision and fall through heightmap terrain (control). |
| `bot_oaxIdle` | 0 | | 1: navigation-mesh bots stand still (they keep only the respawn tap and their view), as test targets. |
| `cg_oaxTestTrail` | "" | cheat | `"shader x0 y0 z0 x1 y1 z1 width lifeMs"`: draws a fixed trail. |
| `cg_oaxVehClip` | 1 | cheat | 0: client prediction ignores vehicle boxes (control). |
| `cg_oaxViewFog` | "" | cheat | `"r g b density start end"`: sets the view fog directly. |
| `cg_oaxSkyPortalTime` | -1 | cheat | Milliseconds; >= 0 pins the sky portal rotation time. |
| `cg_oaxLightTime` | -1 | cheat | Milliseconds; >= 0 evaluates light trajectories at a fixed time. |

`cl_oaxFreezeTime` (the global animation freeze) is an engine cvar; see the
oax engine (github.com/monteslu/oax-engine), docs/test-hooks.md.

## Debug values are not cvars

`debugvalues` lists named values the game modules publish for tests, for
example `g_navbot_*`, `g_ctf_*`, `g_veh_*`, `g_zone*` and `cg_phys_*`,
`cg_vehpred_*`. They look like cvar names but are read-only debug values
kept by the engine; they cannot be set.

## Test commands

Test tools, not gameplay features.

Server console (qagame):

- `physscene <tag> <workers> <ticks> [perturb]`: runs the physics
  determinism scene in a world of its own and publishes `phys_scene_<tag>_*`
  debug values.
- `vehseat <client> <seat>`: puts a client on that seat of the nearest
  vehicle where it is free.
- `vehdrive <throttle> <steer> <msec> [n]`: drives the first driverless
  vehicle (or the n-th vehicle) with these controls for msec, then parks it.
- `vehrocket <x y z> <tx ty tz>`: fires a rocket from a point toward another.
- `assault complete <id>` / `assault limit <seconds>`: completes an Assault
  objective / sets the running round's time limit.
- `vehplace <client> <x y z>`: puts a client there standing still.
- `vehaim <client> <pitch> <yaw>`: turns a client's view; `vehaim <client>
  veh <n>` turns it so its mounted gun points at the n-th vehicle.
- `vehkick <ix iy iz>`: an impulse on the first driven vehicle (else the
  first vehicle).

Client command (cheat): `setviewpos x y z yaw pitch [roll]` places the
player exactly (no lift, no teleporter push); the stock four-number form is
unchanged.

UI console: `ui_graphics` opens the graphics options (resolution, quality)
directly.

Gameplay command (cgame): `toggleview` switches first and third person: in
a vehicle `cg_oaxVehView` (1 and 2), on foot `cg_thirdPerson`. The default
pad binds put it on the right stick click, the keyboard's on V. In a vehicle
`weapon <n>` (the number keys) asks for seat n instead.

Gameplay command (qagame, from the client): `oaxseat <n>` moves the player
to seat n (1 driver, 2 gunner) of its vehicle when that seat is free. In a
seat, jump moves to the next free seat, and use gets out when held.

Client console (cgame):

- `cg_physTest <gib|explode|debris> [count | x y z]`: cosmetic physics
  effects in front of the view (or at x y z).
- `oaxfx <particle decl> x y z [dx dy dz]`: spawns a particle system.
- `oaxdecal <shader> x y z dx dy dz radius [angle [lifeMs [r g b]]]`:
  projects a decal.
