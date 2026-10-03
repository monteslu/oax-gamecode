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
g_oax_place.c: exact player placement for tests (docs/test-hooks.md in
the oax engine (github.com/monteslu/oax-engine)).

`setviewpos x y z yaw pitch [roll]` (cheat; the stock four-number form is
unchanged and still teleports): puts the client's origin exactly at x y z
(no +1 lift, no 400 ups teleporter push, no knockback hold, no teleport
effects or telefrag) with zero velocity, facing yaw / pitch / roll.

Read-back: from that moment, every server frame publishes where the client
really is, after pmove has run on it:
  g_place_request  "client x y z pitch yaw roll"  what was asked
  g_place          "client x y z pitch yaw roll ground vz"  the
                   playerState now (ground entity, 1023 = none, 1022 =
                   world; vertical velocity)
  g_place_frames   server frames since the placement
The view angles come back quantized to the usercmd's 16-bit angles and
with the pitch clamp pmove applies; the origin moves only if the spot was
not a resting one (in the air the player falls, into a floor pmove pushes
out), which the read-back shows. The engine's `cl_view` debug value is the
eye the frame was rendered from.
===========================================================================
*/
#include "g_local.h"

static int placeClient = -1;
static int placeTime;

void G_OAXPlaceExact( gentity_t *ent, const vec3_t origin, const vec3_t angles ) {
	gclient_t *cl = ent->client;

	trap_UnlinkEntity( ent );
	VectorCopy( origin, cl->ps.origin );
	VectorClear( cl->ps.velocity );
	cl->ps.pm_time = 0;
	cl->ps.pm_flags &= ~( PMF_TIME_KNOCKBACK | PMF_TIME_LAND | PMF_TIME_WATERJUMP );
	SetClientViewAngle( ent, (float *)angles );
	cl->ps.eFlags ^= EF_TELEPORT_BIT;	/* no lerp from the old spot */
	G_ResetHistory( ent );
	BG_PlayerStateToEntityState( &cl->ps, &ent->s, qtrue );
	VectorCopy( cl->ps.origin, ent->r.currentOrigin );
	if ( cl->sess.sessionTeam != TEAM_SPECTATOR ) {
		trap_LinkEntity( ent );
	}

	placeClient = ent - g_entities;
	placeTime = level.time;
	BG_OAXDebugSet( "g_place_request", va( "%i %f %f %f %f %f %f", placeClient,
		origin[0], origin[1], origin[2], angles[PITCH], angles[YAW], angles[ROLL] ) );
	G_OAXPlaceFrame();
}

/* every server frame (G_OAXRunFrameEnd): the read-back */
void G_OAXPlaceFrame( void ) {
	gclient_t *cl;

	if ( placeClient < 0 || placeClient >= level.maxclients || !g_entities[placeClient].inuse ) {
		return;
	}
	cl = g_entities[placeClient].client;
	BG_OAXDebugSet( "g_place", va( "%i %f %f %f %f %f %f %i %f", placeClient,
		cl->ps.origin[0], cl->ps.origin[1], cl->ps.origin[2],
		cl->ps.viewangles[PITCH], cl->ps.viewangles[YAW], cl->ps.viewangles[ROLL],
		cl->ps.groundEntityNum, cl->ps.velocity[2] ) );
	BG_OAXDebugSetInt( "g_place_frames", ( level.time - placeTime ) * ( sv_fps.integer > 0 ? sv_fps.integer : 20 ) / 1000 );
}

void G_OAXPlaceInit( void ) {
	placeClient = -1;
	placeTime = 0;
}
