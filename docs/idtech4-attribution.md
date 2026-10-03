# id Tech 4 code in this gamecode

This gamecode (the OpenArena game, cgame and ui modules) also uses code from the id Tech 4 (Doom 3)
GPL source release:

- Source: https://github.com/id-Software/DOOM-3
- Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.
- License: GNU General Public License version 3 (COPYING-GPLv3.txt), with the additional terms in
  DOOM3-ADDITIONAL-TERMS.txt (quoted from DOOM-3's COPYING.txt).

The OpenArena gamecode is licensed "GPL version 2 or (at your
option) any later version", so they combine with GPLv3 code; once id Tech 4
code is included, the gamecode as a whole is distributed under the GPLv3.

No Doom 3 game data (maps, models, textures, sounds) is used, only code.

## Rules for adapted code

1. Keep id Software's copyright and license header on every file that contains
   id Tech 4 code, adapted or not.
2. Under that header, name the DOOM-3 file(s) the code came from (for example
   `Adapted from DOOM-3 neo/game/Mover.cpp`) and say what was changed (ported
   to C, ARB program rewritten as GLSL ES 3.00, and so on).
3. Add a row to the table below in the same commit.

## Files

| File | From DOOM-3 | Changes |
| --- | --- | --- |
| code/game/g_oax_gui.c | neo/game/Player.cpp (idPlayer::UpdateFocus), neo/game/Entity.cpp (idEntity::HandleGuiCommands) | C89 for the QVM; Q3 world and GUI-surface traces; BUTTON_ATTACK is the click; Q3 targets; state via CS_OAX_GUISTATE |
| code/game/bg_oax_traj.h, code/game/bg_oax_traj.c | neo/idlib/math/Extrapolate.h, neo/idlib/math/Interpolate.h, neo/game/physics/Physics_Parametric.cpp | C++ templates to C89 for the QVM; accel/decel phases in closed form from relative ms; parameters packed into a Q3 trajectory_t; spline path via the arc-length table |
| code/game/bg_oax_spline.c | neo/idlib/math/Curve.h, neo/game/Entity.cpp (GetSpline), neo/idlib/math/Vector.cpp (ToAngles) | C89; CatmullRom and order-4 NUBS with uniform knots only; curve from a configstring; GetTimeForLength replaced by a 64-entry Romberg arc-length table |
| code/game/g_oax_mover.c | neo/game/Mover.cpp (idMover move, rotate, spline and callback events) | C89 game module; one packed trajectory per move instead of per-stage extrapolation; stock G_MoverPush; thread numbers via g_oaxMoverDone; no save games; func_oax_mover keyframes are new code |
| code/game/g_oax_triggers.c | neo/game/Trigger.cpp (idTrigger_Count, idTrigger_Timer, idTrigger_EntityName, idTrigger::CallScript), neo/game/Target.cpp (idTarget_SetKeyVal, idTarget_SetShaderParm) | C89 game module on gentity_t; PostEventSec becomes think/nextthink; activators matched by targetname or player name; per-entity deterministic random numbers |
