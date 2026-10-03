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
g_oax_ctfstats.c: match statistics for the outdoor CTF test.

Published as debug values once a second:
  g_ctf_red_taken / g_ctf_blue_taken   times each flag was picked up from
                                       its base or the field (a player
                                       starts holding it)
  g_ctf_red_caps / g_ctf_blue_caps     team scores (captures in CTF)
  g_bots_in_solid                      bot frames whose feet were inside
                                       solid (inside terrain, a brush or a
                                       tree trunk): must stay 0
  g_bots_in_solid_at                   where and when the last of them was
                                       ("x y z @ level ms"), "none" if never
  g_bot_frames                         living bot frames counted
  g_bot_hash                           FNV hash of every bot origin each
                                       frame (native and cart compare it)
===========================================================================
*/
#include "g_local.h"
#include "bg_oax_vehicle.h"

static int ctfHeld[2];		/* client holding the red / blue flag + 1, 0 none */
static int ctfTaken[2];
static int botsInSolid;
static int botFrames;
static char botsInSolidAt[64];
static unsigned botHash;

/* Is p inside something solid to movement? A point trace, not
   trap_PointContents: that one ORs in the brushes of every entity at p, and
   trigger brushes compile with CONTENTS_SOLID (the game gives the entity
   CONTENTS_TRIGGER at spawn), so a player standing in a teleporter's own
   trigger, where noretrigger arrivals put it, would count as in solid. */
static qboolean OAXPointSolid( vec3_t p, int passEntityNum ) {
	trace_t tr;

	trap_Trace( &tr, p, NULL, NULL, p, passEntityNum, CONTENTS_SOLID );
	return tr.startsolid;
}

void G_OAXCtfStatsInit( void ) {
	ctfHeld[0] = ctfHeld[1] = 0;
	ctfTaken[0] = ctfTaken[1] = 0;
	botsInSolid = 0;
	Q_strncpyz( botsInSolidAt, "none", sizeof( botsInSolidAt ) );
	botFrames = 0;
	botHash = 2166136261U;
}

static void OAXCtfHashInt( int v ) {
	int i;
	for ( i = 0; i < 4; i++ ) {
		botHash = ( botHash ^ ( ( v >> ( i * 8 ) ) & 255 ) ) * 16777619U;
	}
}

void G_OAXCtfStatsFrame( void ) {
	int i, f, held[2];
	gentity_t *ent;

	held[0] = held[1] = 0;
	for ( i = 0; i < level.maxclients; i++ ) {
		vec3_t feet;

		ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( ent->client->ps.powerups[PW_REDFLAG] ) {
			held[0] = i + 1;
		}
		if ( ent->client->ps.powerups[PW_BLUEFLAG] ) {
			held[1] = i + 1;
		}
		if ( !( ent->r.svFlags & SVF_BOT ) || ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		botFrames++;
		VectorCopy( ent->r.currentOrigin, feet );
		feet[2] += ent->r.mins[2] + 1.0f;
		/* a vehicle rider has no feet on anything (the vehicle code counts
		   vehicles in solid) */
		if ( !( ent->client->ps.pm_flags & PMF_OAX_VEHICLE ) && OAXPointSolid( feet, ent->s.number ) ) {
			botsInSolid++;
			Q_strncpyz( botsInSolidAt, va( "%.0f %.0f %.0f @ %d", ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2], level.time ), sizeof( botsInSolidAt ) );
		}
		OAXCtfHashInt( i );
		OAXCtfHashInt( (int)( ent->r.currentOrigin[0] * 8.0f ) );
		OAXCtfHashInt( (int)( ent->r.currentOrigin[1] * 8.0f ) );
		OAXCtfHashInt( (int)( ent->r.currentOrigin[2] * 8.0f ) );
	}
	for ( f = 0; f < 2; f++ ) {
		if ( held[f] && held[f] != ctfHeld[f] ) {
			ctfTaken[f]++;
		}
		ctfHeld[f] = held[f];
	}
	if ( level.framenum % 20 ) {
		return;
	}
	BG_OAXDebugSetInt( "g_ctf_red_taken", ctfTaken[0] );
	BG_OAXDebugSetInt( "g_ctf_blue_taken", ctfTaken[1] );
	BG_OAXDebugSetInt( "g_ctf_red_caps", level.teamScores[TEAM_RED] );
	BG_OAXDebugSetInt( "g_ctf_blue_caps", level.teamScores[TEAM_BLUE] );
	BG_OAXDebugSetInt( "g_bots_in_solid", botsInSolid );
	BG_OAXDebugSetInt( "g_bot_frames", botFrames );
	BG_OAXDebugSet( "g_bots_in_solid_at", botsInSolidAt );
	BG_OAXDebugSet( "g_bot_hash", va( "%08x", botHash ) );
}
