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
g_oax_nav.h: navigation-mesh bots (g_oax_navbot.c), the engine's navmesh
syscalls (G_OAX_NAV_*, oax_features token "nav"), navigation from intent
(g_oax_navlinks.c), noretrigger teleporters (g_oax_teleport.c) and the
translocator rule (g_oax_translocator.c).

Maps with heightmap terrain ship without AAS (bspc cannot see terrain);
there the engine builds a Recast/Detour navmesh and these bots path on it.
Where AAS exists, the stock bots run unchanged.
===========================================================================
*/
#ifndef G_OAX_NAV_H
#define G_OAX_NAV_H

#define OAX_NAV_PATH_PARTIAL	1
#define OAX_MIN_WALK_NORMAL		0.7f	/* bg_local.h MIN_WALK_NORMAL */

/* polygon flags and off-mesh link kinds (engine server/nav_oax.h) */
#define OAX_NAV_WALK			0x0001
#define OAX_NAV_HAZARD			0x0002
#define OAX_NAV_TELEPORT		0x0010
#define OAX_NAV_JUMPPAD			0x0020
#define OAX_NAV_LADDER			0x0040
#define OAX_NAV_JUMP			0x0080
#define OAX_NAV_DROP			0x0100
#define OAX_NAV_TRANSLOCATOR	0x1000	/* rule-gated: g_oaxTranslocator */
#define OAX_NAV_DEFAULT			( OAX_NAV_WALK | OAX_NAV_HAZARD | OAX_NAV_TELEPORT | OAX_NAV_JUMPPAD | \
								  OAX_NAV_LADDER | OAX_NAV_JUMP | OAX_NAV_DROP )

/* entityState generic1 bit on a trigger_teleport with "noretrigger 1" */
#define OAX_TELE_NORETRIGGER	1

int trap_OAX_NavStatus( void );
int trap_OAX_NavFindPath( const vec3_t start, const vec3_t goal, float *points, int maxPoints, int *flags );
int trap_OAX_NavNearest( const vec3_t point, const vec3_t halfExtents, vec3_t out );
int trap_OAX_NavRandomPoint( int seed, vec3_t out );
int trap_OAX_NavAddLink( const vec3_t start, const vec3_t end, int kind, float radius, int bidir );
int trap_OAX_NavAddArea( const vec3_t mins, const vec3_t maxs, float cost );
int trap_OAX_NavCommit( void );
int trap_OAX_NavFindPathEx( const vec3_t start, const vec3_t goal, float *points, int *links, int maxPoints, int *flags,
	int include, int exclude );

/* nonzero when bots must use the navmesh: no AAS and the engine built a navmesh */
int G_OAXNavBotsActive( void );
qboolean G_OAXNavBotConnect( int clientNum, float skill );
void G_OAXNavBotFrame( int time );

/* navigation from intent (g_oax_navlinks.c) */
#define OAX_NAV_MAX_LINKS	256

typedef struct {
	int		kind;			/* OAX_NAV_* link kind */
	vec3_t	start, end;		/* on the floor (feet) */
	int		ent;			/* the authoring entity (trigger, pad, zone, route), -1 none */
	float	radius;			/* how close to start counts as there */
} oaxNavLinkInfo_t;

void G_OAXNavLinksInit( void );
void G_OAXNavSpawnCost( gentity_t *ent );
void G_OAXNavLinksFrame( void );
const oaxNavLinkInfo_t *G_OAXNavLink( int index );
int G_OAXNavInclude( void );		/* link kinds bots may use under the server's rules */

/* noretrigger teleporter arrivals (g_oax_teleport.c) */
void G_OAXTeleportInit( void );
void G_OAXTeleportArrived( gentity_t *player );
qboolean G_OAXTeleportLocked( gentity_t *trigger, gentity_t *player );
void G_OAXTeleportLockFrame( gentity_t *player );
int G_OAXTeleportCount( int clientNum );

/* translocator rule (g_oax_translocator.c) */
void G_OAXTranslocatorInit( void );
int G_OAXTranslocatorActive( void );
void G_OAXTranslocatorSpawn( gentity_t *ent );
qboolean G_OAXTranslocatorFire( gentity_t *ent, vec3_t muzzle, vec3_t forward );
void G_OAXTranslocatorFree( gentity_t *owner );
gentity_t *G_OAXTranslocatorBeacon( int clientNum );
float G_OAXTranslocatorSpeed( void );
int G_OAXTranslocatorPredict( const vec3_t start, const vec3_t vel, int frameMsec, vec3_t out );

#endif
