/*
===========================================================================
oax game code
Copyright (C) 2026 Luis Montes

This file is part of the oax game code, a fork of OpenArena's gamecode.
It is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version. The combined game code is distributed under GPLv3.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
/*
===========================================================================
g_oax_mover.h: keyframed and spline movers (func_oax_mover,
func_oax_splinemover, info_oax_spline) and the D3-style mover API that map
scripts drive (see g_oax_mover.c).

The API works on func_oax_mover and func_oax_splinemover entities and is a
no-op (returning 0) on anything else. Times are seconds, as in DOOM-3's
mover script events.
===========================================================================
*/
#ifndef G_OAX_MOVER_H
#define G_OAX_MOVER_H

qboolean G_OAXIsMover( gentity_t *ent );
void G_OAXMoverSetSpeed( gentity_t *ent, float speed );      /* D3 Event_SetMoveSpeed */
void G_OAXMoverSetTime( gentity_t *ent, float seconds );     /* D3 Event_SetMoveTime */
void G_OAXMoverSetAccelTime( gentity_t *ent, float seconds );
void G_OAXMoverSetDecelTime( gentity_t *ent, float seconds );
void G_OAXMoverMoveToPos( gentity_t *ent, const vec3_t pos );
void G_OAXMoverMoveTo( gentity_t *ent, gentity_t *target );
void G_OAXMoverRotateTo( gentity_t *ent, const vec3_t angles );
void G_OAXMoverRotateOnce( gentity_t *ent, const vec3_t delta );
void G_OAXMoverStartSpline( gentity_t *ent, gentity_t *splineEnt );
void G_OAXMoverStopSpline( gentity_t *ent );
int  G_OAXMoverIsMoving( gentity_t *ent );
int  G_OAXMoverIsRotating( gentity_t *ent );
int  G_OAXMoverSetCallback( gentity_t *ent, int threadNum );  /* D3 Event_SetCallback: 1 if it will call back */

/* NULL by default; called when a move or rotation that has a callback
   thread finishes (D3 idThread::ObjectMoveDone) */
extern void ( *g_oaxMoverDone )( gentity_t *ent, int threadNum );

/* hooks (g_oax.c) */
void G_OAXMoverInit( void );
void G_OAXMoverRunFrame( void );

/* spawn functions (g_spawn.c) */
void SP_func_oax_mover( gentity_t *ent );
void SP_func_oax_splinemover( gentity_t *ent );
void SP_info_oax_spline( gentity_t *ent );

#endif
