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
g_oax_zone.c: zone volumes, game side.

func_oax_zone is a brush entity that is never solid and never sent to
clients. Its keys become an infostring in CS_OAX_ZONES + slot, which game
and cgame both parse with BG_OAXZoneParse; player movement reads the zone
through pmove_t.oaxZoneAt (bg_oax_zone.c), so the cgame predicts it exactly.

Keys:
  gravity          absolute gravity inside (ups/s^2), or
  gravity_scale    a scale of the level's gravity
  current          "x y z" ups the zone pulls airborne and swimming players to
  current_accel    how hard it pulls (like sv_airaccelerate), default 1
  current_walk     1: the current drags walking players too (default 0:
                   airborne and swimming only)
  friction_scale   scales ground and water friction, default 1
  fog_color        "r g b" 0..1 view fog while the eye is inside, with
  fog_density, fog_start, fog_end
  reverb           preset name (sound/reverb.txt), and/or
  reverb_delay     decay time (RT60, seconds)
  reverb_gain      wet level 0..1
  priority         where zones overlap the highest wins (then lowest entity)
  damage           points per second to players inside
  ladder           a ladder volume: climb speed in ups (bg_oax_zone.c)
  name             a location name: players inside report it (team overlay,
                   say_team #l) ahead of any target_location
  color            0-7, the name's color, as target_location's count
  ambient          "r g b": the zone's ambient light (renderer only, unified
                   lighting maps; see docs/lights.md in the engine)
  targetname       using the zone toggles it; spawnflag 1 starts it off
===========================================================================
*/
#include "g_local.h"
#include "bg_oax_zone.h"
#include "g_oax_sim.h"
#include "g_oax_nav.h"

static gentity_t *zoneEnts[MAX_OAX_ZONES];
static char       zoneInfo[MAX_OAX_ZONES][MAX_INFO_STRING];
static int        numZoneEnts;
static int        zoneDamageTime[MAX_CLIENTS];

/* zone names as locations: CS_LOCATIONS from the top down (MAX_LOCATIONS - 1
   - slot), so target_location's linkup, which counts up from 1, never meets
   them; the proxies carry what Team_GetLocation's callers read */
static gentity_t  zoneLocations[MAX_OAX_ZONES];

/* published per frame for tests: the first client's zone and its changes */
static int        zoneLast = -2;
static char       zoneLog[96];

static const char *zoneKeys[][2] = {
	{ "gravity", "g" },
	{ "gravity_scale", "gs" },
	{ "current", "c" },
	{ "current_accel", "ca" },
	{ "current_walk", "cw" },
	{ "friction_scale", "f" },
	{ "fog_color", "fc" },
	{ "fog_density", "fd" },
	{ "fog_start", "fs" },
	{ "fog_end", "fe" },
	{ "reverb", "r" },
	{ "reverb_delay", "rd" },
	{ "reverb_gain", "rg" },
	{ "priority", "p" },
	{ "damage", "d" },
	{ "ladder", "l" },
	{ "origin", "o" },
	{ NULL, NULL }
};

static void G_OAXZoneSend( int slot ) {
	trap_SetConfigstring( CS_OAX_ZONES + slot, zoneInfo[slot] );
	BG_OAXZoneParse( slot, zoneInfo[slot] );
}

static void G_OAXZoneUse( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	int slot = self->count;

	Info_SetValueForKey( zoneInfo[slot], "on", atoi( Info_ValueForKey( zoneInfo[slot], "on" ) ) ? "0" : "1" );
	G_OAXZoneSend( slot );
}

/*QUAKED func_oax_zone (.3 .6 .9) ? START_OFF
A volume with its own gravity, current, friction, view fog and reverb.
See g_oax_zone.c for the keys.
*/
void SP_func_oax_zone( gentity_t *ent ) {
	char *info;
	char *v;
	int   i, slot;

	G_OAXNavSpawnCost( ent );	/* "navcost" of a damage zone (g_oax_navlinks.c) */

	if ( numZoneEnts >= MAX_OAX_ZONES ) {
		G_Printf( "func_oax_zone: more than %i zones, ignored\n", MAX_OAX_ZONES );
		G_FreeEntity( ent );
		return;
	}
	if ( !ent->model || ent->model[0] != '*' ) {
		G_Printf( "func_oax_zone: needs brushes\n" );
		G_FreeEntity( ent );
		return;
	}
	slot = numZoneEnts++;
	zoneEnts[slot] = ent;
	ent->count = slot;

	trap_SetBrushModel( ent, ent->model );
	G_SetOrigin( ent, ent->s.origin );
	ent->r.contents = 0;
	ent->r.svFlags |= SVF_NOCLIENT;
	trap_UnlinkEntity( ent );   /* only point tests reach it */
	ent->use = G_OAXZoneUse;

	info = zoneInfo[slot];
	info[0] = '\0';
	Info_SetValueForKey( info, "on", ( ent->spawnflags & 1 ) ? "0" : "1" );
	Info_SetValueForKey( info, "e", va( "%i", ent->s.number ) );
	Info_SetValueForKey( info, "m", va( "%i", atoi( ent->model + 1 ) ) );
	Info_SetValueForKey( info, "lo", va( "%i %i %i",
		(int)( ent->s.origin[0] + ent->r.mins[0] ) - 1,
		(int)( ent->s.origin[1] + ent->r.mins[1] ) - 1,
		(int)( ent->s.origin[2] + ent->r.mins[2] ) - 1 ) );
	Info_SetValueForKey( info, "hi", va( "%i %i %i",
		(int)( ent->s.origin[0] + ent->r.maxs[0] ) + 1,
		(int)( ent->s.origin[1] + ent->r.maxs[1] ) + 1,
		(int)( ent->s.origin[2] + ent->r.maxs[2] ) + 1 ) );
	for ( i = 0; zoneKeys[i][0]; i++ ) {
		if ( G_SpawnString( zoneKeys[i][0], "", &v ) && v[0] ) {
			Info_SetValueForKey( info, zoneKeys[i][1], v );
		}
	}
	memset( &zoneLocations[slot], 0, sizeof( zoneLocations[slot] ) );
	if ( G_SpawnString( "name", "", &v ) && v[0] && MAX_LOCATIONS - 1 - slot > 0 ) {
		zoneLocations[slot].message = G_NewString( v );
		zoneLocations[slot].health = MAX_LOCATIONS - 1 - slot;
		G_SpawnInt( "color", "0", &zoneLocations[slot].count );
		trap_SetConfigstring( CS_LOCATIONS + zoneLocations[slot].health, zoneLocations[slot].message );
	}
	G_OAXZoneSend( slot );
}

/* game-side point test: SV_EntityContact runs CM_TransformedBoxTrace with a
   zero-size box at point, as the cgame's test does */
static int G_OAXZoneContact( const bgOAXZone_t *z, const vec3_t point ) {
	return trap_EntityContact( point, point, &g_entities[z->entityNum] );
}

static int G_OAXZoneAt( const vec3_t point ) {
	return BG_OAXZoneAtPoint( point, G_OAXZoneContact );
}

/*
=================
G_OAXZoneLocation

The named zone ent stands in (Team_GetLocation asks first), or NULL.
=================
*/
gentity_t *G_OAXZoneLocation( gentity_t *ent ) {
	int zone;

	if ( !bg_oaxNumZones || !ent ) {
		return NULL;
	}
	zone = G_OAXZoneAt( ent->r.currentOrigin );
	if ( zone < 0 || !zoneLocations[zone].health ) {
		return NULL;
	}
	return &zoneLocations[zone];
}

void G_OAXZonePmove( pmove_t *pm ) {
	pm->oaxZoneAt = 0;
	if ( bg_oaxNumZones ) {
		pm->oaxZoneAt = G_OAXZoneAt;
	}
}

/*
=================
G_OAXZoneFrame

Zone damage, and the first client's zone for tests (g_zone, and g_zone_log:
"commandTime:slot" at every change).
=================
*/
void G_OAXZoneFrame( void ) {
	int        i, zone;
	gentity_t *ent;
	int        first = 1;

	if ( !bg_oaxNumZones ) {
		return;
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		zone = G_OAXZoneAt( ent->client->ps.origin );
		if ( first ) {
			first = 0;
			if ( zone != zoneLast ) {
				zoneLast = zone;
				if ( strlen( zoneLog ) < 66 ) {   /* the first transitions only, within zoneLog[96] */
					Q_strcat( zoneLog, sizeof( zoneLog ), va( "%i:%i ", ent->client->ps.commandTime, zone ) );
				}
				BG_OAXDebugSet( "g_zone_log", zoneLog );
			}
			BG_OAXDebugSetInt( "g_zone", zone );
			{
				/* the location a team message would name, for tests */
				char loc[64];

				BG_OAXDebugSet( "g_location", Team_GetLocationMsg( ent, loc, sizeof( loc ) ) ? loc : "none" );
			}
		}
		if ( zone >= 0 && bg_oaxZones[zone].damage > 0 && ent->health > 0 &&
		     ent->client->ps.pm_type == PM_NORMAL && level.time >= zoneDamageTime[i] ) {
			gentity_t *z = &g_entities[bg_oaxZones[zone].entityNum];
			zoneDamageTime[i] = level.time + 1000;
			G_Damage( ent, z, z, NULL, NULL, bg_oaxZones[zone].damage, DAMAGE_NO_PROTECTION, MOD_TRIGGER_HURT );
		}
	}
}
