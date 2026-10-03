/*
===========================================================================
oax game code
Copyright (C) 2026 Luis Montes

This file is part of the oax game code, a fork of OpenArena's gamecode.
It is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version, as the id Tech 4 code it follows.

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
g_oax_portal.c: area portals that scripts and distance close (oax
"portal" feature).

New code for oax (GPLv3, as the id Tech 4 code it follows): the behavior
follows DOOM-3's idFuncPortal (neo/game/Misc.cpp: use toggles the portal,
"start_on" there is "start_closed" here) and idMover's openPortal /
closePortal script events. Quake III already culls snapshots and the
renderer by area and lets doors open area portals; this adds:

  func_oax_portal   a brush entity placed across a common/areaportal brush
                    keys: "start_closed" (1 = closed at map start),
                    "portal_dist" (> 0: closed while every player is
                    farther than this from it), "targetname" (scripts:
                    $name.openPortal() / closePortal(); use toggles)

The engine's area portal state is a reference count, so every change here
goes through G_OAXPortalSet, which only calls trap_AdjustAreaPortalState
when an entity's own contribution changes.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_script.h"

#define PORTAL_OPEN			1		/* ent->count bit: the scripted/use state */
#define PORTAL_APPLIED		2		/* ent->count bit: this entity holds the portal open */

static qboolean	portalOpenBy[MAX_GENTITIES];	/* entities other than func_oax_portal */
static float		portalDist[MAX_GENTITIES];		/* portal_dist */

static qboolean IsOAXPortal( gentity_t *ent ) {
	return ent->classname && !Q_stricmp( ent->classname, "func_oax_portal" );
}

static void ApplyPortal( gentity_t *ent, qboolean open ) {
	qboolean held = ( ent->count & PORTAL_APPLIED ) != 0;

	if ( open == held ) {
		return;
	}
	trap_AdjustAreaPortalState( ent, open );
	if ( open ) {
		ent->count |= PORTAL_APPLIED;
	} else {
		ent->count &= ~PORTAL_APPLIED;
	}
	BG_OAXDebugSetInt( va( "g_portal_%s", ent->targetname ? ent->targetname : va( "%d", (int)( ent - g_entities ) ) ), open ? 1 : 0 );
}

/* whether a player is within portal_dist (portalDist[ent - g_entities]) of the portal */
static qboolean PlayerNear( gentity_t *ent ) {
	int			i;
	vec3_t		center, d;
	gclient_t	*cl;

	VectorAdd( ent->r.absmin, ent->r.absmax, center );
	VectorScale( center, 0.5f, center );
	for ( i = 0; i < level.maxclients; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected != CON_CONNECTED || !g_entities[i].inuse ) {
			continue;
		}
		VectorSubtract( cl->ps.origin, center, d );
		if ( VectorLength( d ) <= portalDist[ent - g_entities] ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void UpdatePortal( gentity_t *ent ) {
	qboolean open = ( ent->count & PORTAL_OPEN ) != 0;

	if ( open && portalDist[ent - g_entities] > 0 && !PlayerNear( ent ) ) {
		open = qfalse;
	}
	ApplyPortal( ent, open );
}

/*
================
G_OAXPortalSet

Scripts' openPortal/closePortal on any entity. A func_oax_portal keeps the
state (distance closing still applies); any other entity (a door, say)
opens or closes the portal it spans, once.
================
*/
void G_OAXPortalSet( gentity_t *ent, qboolean open ) {
	int n;

	if ( !ent ) {
		return;
	}
	if ( IsOAXPortal( ent ) ) {
		if ( open ) {
			ent->count |= PORTAL_OPEN;
		} else {
			ent->count &= ~PORTAL_OPEN;
		}
		UpdatePortal( ent );
		return;
	}
	n = ent - g_entities;
	if ( portalOpenBy[n] == open ) {
		return;
	}
	portalOpenBy[n] = open;
	trap_AdjustAreaPortalState( ent, open );
}

/* D3 idFuncPortal::Event_Activate: use toggles */
static void Use_func_oax_portal( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	ent->count ^= PORTAL_OPEN;
	UpdatePortal( ent );
}

static void Think_func_oax_portal( gentity_t *ent ) {
	/* the first frame: the entity is linked, its areas are known */
	UpdatePortal( ent );
}

void SP_func_oax_portal( gentity_t *ent ) {
	int closed;

	G_SpawnInt( "start_closed", "0", &closed );
	G_SpawnFloat( "portal_dist", "0", &portalDist[ent - g_entities] );

	if ( ent->model && ent->model[0] == '*' ) {
		trap_SetBrushModel( ent, ent->model );
	}
	ent->r.contents = 0;
	ent->r.svFlags = SVF_NOCLIENT;
	trap_LinkEntity( ent );

	ent->count = closed ? 0 : PORTAL_OPEN;
	portalOpenBy[ent - g_entities] = qfalse;
	ent->use = Use_func_oax_portal;
	ent->think = Think_func_oax_portal;
	ent->nextthink = level.time + FRAMETIME;
}

/*
================
G_OAXPortalRunFrame

Distance closing (portal_dist).
================
*/
void G_OAXPortalRunFrame( void ) {
	int			i;
	gentity_t	*ent;

	for ( i = MAX_CLIENTS, ent = g_entities + MAX_CLIENTS; i < level.num_entities; i++, ent++ ) {
		if ( !ent->inuse || portalDist[ent - g_entities] <= 0 || !IsOAXPortal( ent ) || ent->nextthink ) {
			continue;
		}
		UpdatePortal( ent );
	}
}
