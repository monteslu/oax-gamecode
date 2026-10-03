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
g_oax_teleport.c: noretrigger teleporter arrivals.

A trigger_teleport with the key "noretrigger 1" does not send on a player
who ARRIVED inside it by teleport, until that player has left it. That
lets a map put the arrival on the paired teleporter (as converted maps
often do): the classic Q3 rule sends the player straight back, so a pair
whose destinations sit inside each other's triggers ping-pongs forever.

Without the key nothing changes: classic teleporters behave exactly as
before, and stock maps never carry the key.

- On every arrival (TeleportPlayer: teleporters, target_teleporter, the
  translocator) the noretrigger triggers the player's box touches are
  locked for that player.
- Every player move (ClientThink, before G_TouchTriggers) drops the locks
  the box no longer touches.
- trigger_teleporter_touch skips a locked trigger.

The cgame does not predict hyperspace for noretrigger triggers
(entityState generic1 & OAX_TELE_NORETRIGGER), or a player standing on an
arrival would see a blank view.

Debug values: g_teleports_<client> for the first four clients (the number
of teleports), g_tele_locks (locks held right now).
===========================================================================
*/
#include "g_local.h"
#include "g_oax_nav.h"

#define TELE_MAX_LOCKS 4

static int teleLock[MAX_CLIENTS][TELE_MAX_LOCKS];	/* entity number + 1 */
static int teleCount[MAX_CLIENTS];

void G_OAXTeleportInit( void ) {
	memset( teleLock, 0, sizeof( teleLock ) );
	memset( teleCount, 0, sizeof( teleCount ) );
}

int G_OAXTeleportCount( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return 0;
	}
	return teleCount[clientNum];
}

static void G_OAXTeleportPublish( void ) {
	int i, j, n = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		for ( j = 0; j < TELE_MAX_LOCKS; j++ ) {
			if ( teleLock[i][j] ) {
				n++;
			}
		}
	}
	BG_OAXDebugSetInt( "g_tele_locks", n );
}

/* the player's box where G_TouchTriggers tests it */
static void G_OAXPlayerBox( gentity_t *player, vec3_t mins, vec3_t maxs ) {
	VectorAdd( player->client->ps.origin, player->r.mins, mins );
	VectorAdd( player->client->ps.origin, player->r.maxs, maxs );
}

void G_OAXTeleportArrived( gentity_t *player ) {
	int touch[MAX_GENTITIES], num, i, n = 0, c;
	vec3_t mins, maxs;

	if ( !player->client ) {
		return;
	}
	c = player->s.number;
	memset( teleLock[c], 0, sizeof( teleLock[c] ) );
	teleCount[c]++;
	if ( c < 4 ) {
		BG_OAXDebugSetInt( va( "g_teleports_%i", c ), teleCount[c] );
	}
	G_OAXPlayerBox( player, mins, maxs );
	num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
	for ( i = 0; i < num && n < TELE_MAX_LOCKS; i++ ) {
		gentity_t *hit = &g_entities[touch[i]];
		if ( hit->s.eType != ET_TELEPORT_TRIGGER || !( hit->s.generic1 & OAX_TELE_NORETRIGGER ) ) {
			continue;
		}
		if ( !trap_EntityContact( mins, maxs, hit ) ) {
			continue;
		}
		teleLock[c][n++] = touch[i] + 1;
	}
	G_OAXTeleportPublish();
}

qboolean G_OAXTeleportLocked( gentity_t *trigger, gentity_t *player ) {
	int j, c;

	if ( !player->client || !( trigger->s.generic1 & OAX_TELE_NORETRIGGER ) ) {
		return qfalse;
	}
	c = player->s.number;
	for ( j = 0; j < TELE_MAX_LOCKS; j++ ) {
		if ( teleLock[c][j] == trigger->s.number + 1 ) {
			return qtrue;
		}
	}
	return qfalse;
}

void G_OAXTeleportLockFrame( gentity_t *player ) {
	int j, c, changed = 0;
	vec3_t mins, maxs;

	if ( !player->client ) {
		return;
	}
	c = player->s.number;
	for ( j = 0; j < TELE_MAX_LOCKS; j++ ) {
		if ( teleLock[c][j] ) {
			break;
		}
	}
	if ( j == TELE_MAX_LOCKS ) {
		return;
	}
	G_OAXPlayerBox( player, mins, maxs );
	for ( j = 0; j < TELE_MAX_LOCKS; j++ ) {
		gentity_t *hit;
		if ( !teleLock[c][j] ) {
			continue;
		}
		hit = &g_entities[teleLock[c][j] - 1];
		/* dead players and spectators keep nothing locked */
		if ( !hit->inuse || player->client->ps.stats[STAT_HEALTH] <= 0 || !trap_EntityContact( mins, maxs, hit ) ) {
			teleLock[c][j] = 0;
			changed = 1;
		}
	}
	if ( changed ) {
		G_OAXTeleportPublish();
	}
}
