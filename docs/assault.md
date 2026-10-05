# Assault

Assault (`g_gametype 14`) is an attack-and-defend team mode. One team attacks a
base against the clock and the other defends it. The attackers have to
complete the map's objectives in order: destroy something, reach a place, or
work a console. Completing the final objective wins the round.

The code is `code/game/g_oax_assault.c` (rules), `code/cgame/cg_oax_assault.c`
(HUD) and `code/game/bg_oax_assault.h` (what they share).

## A match

A match is two rounds on the same map. The teams swap roles between them.

1. **Round 1.** Red attacks (or blue, with `g_oaxAssaultFirst 1`). The round
   has the map's time limit. Its result is either the time the attackers took
   or a failure.
2. **Round 2.** The map restarts with the roles swapped:
   - If round 1 succeeded, the new attackers must finish faster than that
     time, which becomes their limit.
   - If round 1 failed, they get the full limit and only need to finish.
3. **The result:**

   | Round 1 | Round 2 | Winner |
   | --- | --- | --- |
   | made it | beat the time | round 2's attackers |
   | made it | did not | round 1's attackers |
   | failed | made it | round 2's attackers |
   | failed | failed | a draw |

The winner gets one team point. Then the intermission follows. Between the
rounds the result shows for a few seconds. Each round's clock starts 3 seconds
after the round does.

## Playing

- **Objectives:**
  - Each objective scores 10 points for the player who completes it.
  - Only the attacking team's damage counts on a destroy objective.
  - A use objective is worked by holding the use button (X on a pad, E or
    Enter on the keyboard) next to it. The progress bar fills over its use
    time and slips back slowly if nobody holds it.
- **The HUD:**
  - Top centre: your role (ATTACK or DEFEND), the clock, the round, and in
    round 2 the time to beat.
  - Left: the objectives. The open ones show their health or progress, the
    finished ones are ticked off, and the later ones are dimmed.
  - In the world: a marker over every open objective, with what to do there
    and how far away it is.
  - `cg_oaxAssaultHud 0` hides all of this.
- **Spawns:** they move as objectives fall. Attackers spawn closer to the next
  objective, and defenders fall back.
- **Bots:** on navmesh maps (no AAS), bots play Assault:
  - Attacking bots go for the first open objective. They shoot destroy
    objectives once they see them, and stand at use objectives holding use.
    Like other bots, they take vehicles for long trips.
  - Defending bots spread over the active defender spawn spots, which a map
    puts next to the objectives, and fight from there.

## Maps

`oax_assault` (in the engine's test maps) is the example map. It is a valley
climbing to a walled fortress, with three objectives:
1. The gate generator, which opens the gate.
2. The keep controls, held for 5 seconds, which open the keep.
3. The reactor core.

With the vehicle rule (`g_oaxVehicles 1`), the attackers also get a buggy and
a hover craft.

The entities a map uses:

### func_oax_objective

A brush entity, or a point entity for reach and trigger objectives.

| Key | Meaning |
| --- | --- |
| `type` | `destroy`, `reach`, `use` or `trigger` |
| `id` | a short name that spawn points refer to |
| `name` | what the HUD calls it ("the gate generator") |
| `order` | objectives open when every objective with a lower order is done; equal orders can be done in any order (default 1) |
| `final` | 1: completing it wins the round |
| `health` | destroy: hit points (default 600) |
| `radius` | use: how close to stand (default 96); a point reach objective: how close to get (default 128) |
| `usetime` | use: seconds of holding use (default 4) |
| `message` | announced to everyone when it is done |
| `target`, `call` | fired when it is done: open doors (`func_door` with a `targetname` and `wait -1`), start a map script |

- A destroy objective is solid. When it falls it disappears in explosions.
- A brush reach objective is an invisible trigger volume. Use `common/trigger`
  on its brush.
- A use objective is drawn as its brush (a console, for example).
- A trigger objective is completed when something targets it.

### info_oax_assault_spawn

A spawn point. Players spawn at a random active spot of their role.

| Key | Meaning |
| --- | --- |
| `role` | `attack` or `defend` |
| `after` | an objective id: the spot is active once that objective is done |
| `until` | an objective id: the spot is active until that objective is done |
| `angle` | facing |

### info_oax_assault

Optional. Holds the map's Assault settings.

| Key | Meaning |
| --- | --- |
| `time` | the round's time limit in seconds (default 480) |
| `message` | the attackers' briefing, shown when the round starts |

A map lists the mode in its arena file (`type "assault"`) so that the start
server menu offers it.

Doors and other movers are not obstacles to the navmesh. Lay out the stages so
that bots never need to path through a door that is still shut. On the example
map, each stage's objective and spawns are on the near side of the door that
the stage opens.

## Settings

| Cvar | Default | What it does |
| --- | --- | --- |
| `g_oaxAssaultTime` | 0 | The round's time limit in seconds; 0 uses the map's `info_oax_assault` time (or 480). |
| `g_oaxAssaultFirst` | 0 | 0: red attacks first; 1: blue does. |
| `cg_oaxAssaultHud` | 1 | The Assault HUD. |

`g_oaxAssaultState` is internal: it carries round 1's result across the map
restart.

For tests:
- The server command `assault complete <id>` completes an objective.
- The server command `assault limit <seconds>` sets the running round's limit.
- The debug values `g_as_*` and `cg_as_hud` publish the round, phase,
  attackers, each objective's state, the outcome and the winner.
