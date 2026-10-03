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
g_oax_skyportal.c: misc_oax_skyportal, a sky seen through a camera in a
sealed room of the same map (a sky portal camera).

QUAKED misc_oax_skyportal (.6 .7 .7) (-8 -8 -8) (8 8 8)
Place in the sky room. Sky brushes elsewhere use a shader with
`surfaceparm sky` and `surfaceparm skyportal` plus a skyparms box, which
is what a stock engine or a stock cgame shows instead.
"fov"     sky camera field of view (0 = the player's)
"rotate"  "pitch yaw roll" degrees per second the sky turns
"scale"   parallax divisor: the camera moves (view - anchor) / scale;
          0 = the camera stays put
"target"  an info_notnull or target_position used as the anchor
          (default: the world origin)

The entity is broadcast with SVF_PORTAL, so the oax engine sends what its
camera sees (the sky room's movers) to every client. Its parameters go to
CS_OAX_INFO as sp* keys; cgame does the rendering (cg_oax_render.c).
===========================================================================
*/
#include "g_local.h"

static void G_OAXSetInfoKey( const char *key, const char *value ) {
	char info[MAX_INFO_STRING];

	trap_GetConfigstring( CS_OAX_INFO, info, sizeof( info ) );
	Info_SetValueForKey( info, key, value );
	trap_SetConfigstring( CS_OAX_INFO, info );
}

static void SkyPortal_Link( gentity_t *ent ) {
	vec3_t		anchor;
	gentity_t	*t;
	float		fov, scale;
	vec3_t		rotate;

	VectorClear( anchor );
	if ( ent->target ) {
		t = G_Find( NULL, FOFS( targetname ), ent->target );
		if ( t ) {
			VectorCopy( t->s.origin, anchor );
		} else {
			G_Printf( "misc_oax_skyportal: no target %s\n", ent->target );
		}
	}

	fov = ent->random;
	scale = ent->speed;
	VectorCopy( ent->movedir, rotate );

	G_OAXSetInfoKey( "sp", va( "%i %i %i", (int)ent->s.origin2[0], (int)ent->s.origin2[1], (int)ent->s.origin2[2] ) );
	G_OAXSetInfoKey( "spr", va( "%g %g %g", rotate[0], rotate[1], rotate[2] ) );
	G_OAXSetInfoKey( "spf", va( "%g", fov ) );
	G_OAXSetInfoKey( "sps", va( "%g", scale ) );
	G_OAXSetInfoKey( "spa", va( "%i %i %i", (int)anchor[0], (int)anchor[1], (int)anchor[2] ) );
	BG_OAXDebugSetInt( "g_skyportal", 1 );
	ent->think = 0;
}

void SP_misc_oax_skyportal( gentity_t *ent ) {
	G_SpawnFloat( "fov", "0", &ent->random );
	G_SpawnFloat( "scale", "0", &ent->speed );
	G_SpawnVector( "rotate", "0 0 0", ent->movedir );

	ent->s.eType = ET_INVISIBLE;
	ent->r.svFlags = SVF_PORTAL | SVF_BROADCAST;
	VectorCopy( ent->s.origin, ent->s.origin2 );
	G_SetOrigin( ent, ent->s.origin );
	trap_LinkEntity( ent );

	// the anchor may spawn after us
	ent->think = SkyPortal_Link;
	ent->nextthink = level.time + FRAMETIME;
}
