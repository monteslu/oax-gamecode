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
g_oax_navlinks.c: navigation from authored intent.

On maps whose bots path on the engine's navmesh (g_oax_navbot.c), the
map's entities say where movement goes beyond walking. They become
off-mesh links and cost volumes of the navmesh (G_OAX_NAV_ADDLINK /
ADDAREA / COMMIT), built once the level has settled (the jump pads have
aimed, AimAtTarget). See docs/navigation.md in
the oax engine (github.com/monteslu/oax-engine).

Links (start and end on the floor, where the player's feet are):
- trigger_teleport -> its destination. A destination inside a CLASSIC
  trigger_teleport would ping-pong, so it gets no link (bspc drops those
  reachabilities too); inside a noretrigger trigger it is fine.
- trigger_push -> where the push really lands: the pad's velocity
  (s.origin2, set by AimAtTarget from g_gravity) flown through the world
  in pmove-sized steps with the player's box, sliding along walls, until
  it lands on walkable ground. Not bspc's 1.1x guess.
- func_oax_zone with "ladder": from the floor below the volume to the
  walkable ledge beside its top.
- info_oax_route -> its target info_oax_route: movement the game allows by
  rule. "kind" jump (default), drop, or translocator (usable only while
  g_oaxTranslocator is on); "bidir" 1 for both ways; "radius".

Cost volumes (hazards; the walkable surface stays on the mesh, so items in
a hazard stay reachable, unlike bspc's lava):
- trigger_hurt that is on: "navcost" (default 10: one unit inside costs
  ten units of walking); a hurt volume that kills at once (dmg >= 100 per
  hit) is removed from the mesh, like a pit. "navcost -1" removes any.
- func_oax_zone with "damage": the same, default 10; a zone that kills
  within a second (damage >= 100 per second) is removed from the mesh.

Links that cannot be made (an end in solid, no floor below a marker, a
pad arc that never lands) are printed and listed in g_nav_links_skipped.

Debug values: g_nav_links (links submitted), g_nav_links_pingpong
(teleporters left out because the arrival would re-trigger), g_nav_hazards,
g_nav_link_<i> ("kind sx sy sz ex ey ez" for the first 16 links).
===========================================================================
*/
#include "g_local.h"
#include "g_oax_nav.h"
#include "bg_oax_zone.h"

#define NAVLINK_SIM_MSEC	8		/* pmove_msec */
#define NAVLINK_SIM_TIME	4000
#define NAVLINK_HAZARD_COST	10.0f

static oaxNavLinkInfo_t	navLinks[OAX_NAV_MAX_LINKS];
static int				numNavLinks;
static int				navLinksBuilt;
static float			navCost[MAX_GENTITIES];		/* "navcost" keys, read at spawn */
static byte				navCostSet[MAX_GENTITIES];
/* why the last G_OAXNavFloor failed: "solid", "no floor", "steep" */
static const char *navFloorWhy;
static char navSkipped[960];	/* links left out, for g_nav_links_skipped */
static int navNumSkipped;

/* spawn functions of hazard entities call this while their keys are readable */
void G_OAXNavSpawnCost( gentity_t *ent ) {
	char *v;

	if ( G_SpawnString( "navcost", "", &v ) && v[0] ) {
		navCost[ent->s.number] = atof( v );
		navCostSet[ent->s.number] = 1;
	} else {
		navCostSet[ent->s.number] = 0;
	}
}

void G_OAXNavLinksInit( void ) {
	memset( navLinks, 0, sizeof( navLinks ) );
	numNavLinks = 0;
	navLinksBuilt = 0;
	navSkipped[0] = 0;
	navNumSkipped = 0;
	navFloorWhy = "";
	/* navCost/navCostSet are filled while entities spawn, before this runs: not cleared here */
}

const oaxNavLinkInfo_t *G_OAXNavLink( int index ) {
	if ( index < 0 || index >= numNavLinks ) {
		return NULL;
	}
	return &navLinks[index];
}

int G_OAXNavInclude( void ) {
	return OAX_NAV_DEFAULT | ( G_OAXTranslocatorActive() ? OAX_NAV_TRANSLOCATOR : 0 );
}

static const char *G_OAXNavKindName( int kind ) {
	switch ( kind ) {
	case OAX_NAV_TELEPORT: return "teleport";
	case OAX_NAV_JUMPPAD: return "jumppad";
	case OAX_NAV_LADDER: return "ladder";
	case OAX_NAV_JUMP: return "jump";
	case OAX_NAV_DROP: return "drop";
	case OAX_NAV_TRANSLOCATOR: return "translocator";
	}
	return "?";
}

/* the floor under a player-origin point: feet position, 0 if none within range */

/* a player box that starts in solid (a marker sunk into a floor or a
   wall) is lifted, up to 48 units, before it looks down */
static int G_OAXNavFloor( const vec3_t origin, float range, vec3_t feet ) {
	trace_t tr;
	vec3_t mins = { -15, -15, -24 }, maxs = { 15, 15, 32 }, start, end;
	int lift;

	for ( lift = 0; lift <= 48; lift += 16 ) {
		VectorCopy( origin, start );
		start[2] += lift;
		VectorCopy( start, end );
		end[2] -= range + lift;
		trap_Trace( &tr, start, mins, maxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( !tr.startsolid && !tr.allsolid ) {
			break;
		}
	}
	if ( tr.startsolid || tr.allsolid ) {
		navFloorWhy = "solid";
		return 0;
	}
	if ( tr.fraction >= 1.0f ) {
		/* say how far down the floor really is, if there is one */
		vec3_t deep;
		trace_t tr2;
		VectorCopy( tr.endpos, deep );
		deep[2] -= 8192.0f;
		trap_Trace( &tr2, tr.endpos, mins, maxs, deep, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr2.fraction < 1.0f && !tr2.startsolid ) {
			navFloorWhy = va( "no floor within %i (the floor is %i below the marker)", (int)range,
				(int)( origin[2] - tr2.endpos[2] ) );
		} else {
			navFloorWhy = va( "no floor within %i (none below)", (int)range );
		}
		return 0;
	}
	if ( tr.plane.normal[2] < OAX_MIN_WALK_NORMAL ) {
		navFloorWhy = "steep";
		return 0;
	}
	VectorCopy( tr.endpos, feet );
	feet[2] += mins[2];
	return 1;
}

/* a link that could not be made is reported, never dropped silently */
static void G_OAXNavSkip( const char *what, gentity_t *ent, const char *end, const char *why ) {
	char item[96];

	navNumSkipped++;
	Com_sprintf( item, sizeof( item ), "%s%s#%i %s: %s", navSkipped[0] ? "; " : "", what, ent ? ent->s.number : -1, end, why );
	if ( strlen( navSkipped ) + strlen( item ) < sizeof( navSkipped ) - 1 ) {
		Q_strcat( navSkipped, sizeof( navSkipped ), item );
	}
	G_Printf( "navmesh link skipped: %s#%i %s: %s\n", what, ent ? ent->s.number : -1, end, why );
}

/* the floor under a brush entity's footprint centre, searched from its top */
static int G_OAXNavVolumeFloor( gentity_t *ent, vec3_t feet ) {
	vec3_t o;

	o[0] = ( ent->r.absmin[0] + ent->r.absmax[0] ) * 0.5f;
	o[1] = ( ent->r.absmin[1] + ent->r.absmax[1] ) * 0.5f;
	o[2] = ent->r.absmin[2] + 40.0f;	/* a player standing at the bottom, feet a little above it */
	return G_OAXNavFloor( o, 128.0f, feet );
}

static void G_OAXNavAdd( int kind, const vec3_t start, const vec3_t end, float radius, int bidir, int ent ) {
	int idx;

	if ( numNavLinks >= OAX_NAV_MAX_LINKS ) {
		return;
	}
	/* an Assault-gated teleporter's link carries a bit of its own, which
	   bots it is closed to exclude (G_OAXAssaultNavExclude) */
	idx = trap_OAX_NavAddLink( start, end, kind | ( kind == OAX_NAV_TELEPORT ? G_OAXAssaultGateNavBit( ent ) : 0 ), radius, bidir );
	if ( idx != numNavLinks ) {
		return;		/* the engine is out of room: keep both lists in step */
	}
	navLinks[idx].kind = kind;
	VectorCopy( start, navLinks[idx].start );
	VectorCopy( end, navLinks[idx].end );
	navLinks[idx].ent = ent;
	navLinks[idx].radius = radius > 0 ? radius : 32.0f;
	if ( idx < 16 ) {
		BG_OAXDebugSet( va( "g_nav_link_%i", idx ), va( "%s %i %i %i %i %i %i", G_OAXNavKindName( kind ),
			(int)start[0], (int)start[1], (int)start[2], (int)end[0], (int)end[1], (int)end[2] ) );
	}
	numNavLinks++;
}

/* is a player box at origin inside a trigger_teleport without noretrigger? */
static int G_OAXNavInClassicTeleporter( const vec3_t origin ) {
	int touch[MAX_GENTITIES], num, i;
	vec3_t mins, maxs;

	VectorSet( mins, origin[0] - 15, origin[1] - 15, origin[2] - 24 );
	VectorSet( maxs, origin[0] + 15, origin[1] + 15, origin[2] + 32 );
	num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
	for ( i = 0; i < num; i++ ) {
		gentity_t *hit = &g_entities[touch[i]];
		if ( hit->s.eType == ET_TELEPORT_TRIGGER && !( hit->s.generic1 & OAX_TELE_NORETRIGGER ) &&
			!( hit->spawnflags & 1 ) && trap_EntityContact( mins, maxs, hit ) ) {
			return 1;
		}
	}
	return 0;
}

static float G_OAXNavVolumeRadius( gentity_t *ent ) {
	float r = ( ent->r.absmax[0] - ent->r.absmin[0] ) * 0.5f;

	if ( ( ent->r.absmax[1] - ent->r.absmin[1] ) * 0.5f < r ) {
		r = ( ent->r.absmax[1] - ent->r.absmin[1] ) * 0.5f;
	}
	r += 16.0f;		/* the player touches it from a box half width away */
	if ( r < 24.0f ) {
		r = 24.0f;
	}
	if ( r > 96.0f ) {
		r = 96.0f;
	}
	return r;
}

/* the next entity whose targetname is name after from (NULL: the first), in
   entity order. Never G_PickTarget: that picks with rand(), and building the
   links must not move the game's random stream (stock maps stay identical). */
static gentity_t *G_OAXNavNextTarget( gentity_t *from, const char *name ) {
	return G_Find( from, FOFS( targetname ), name );
}

static int G_OAXNavTeleporters( void ) {
	int i, pingpong = 0;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i], *dest;
		vec3_t start, end, arrive;

		/* spectator-only and seamless warps (spawnflags 1, 4) are not routes */
		if ( !ent->inuse || ent->s.eType != ET_TELEPORT_TRIGGER || ( ent->spawnflags & 5 ) || !ent->target ) {
			continue;
		}
		if ( !G_OAXNavVolumeFloor( ent, start ) ) {
			G_OAXNavSkip( "teleporter", ent, "trigger", navFloorWhy );
			continue;
		}
		/* a teleporter with several destinations picks one at random: a link to each */
		for ( dest = G_OAXNavNextTarget( NULL, ent->target ); dest; dest = G_OAXNavNextTarget( dest, ent->target ) ) {
			/* TeleportPlayer puts the origin one unit above the destination */
			VectorCopy( dest->s.origin, arrive );
			arrive[2] += 1;
			if ( G_OAXNavInClassicTeleporter( arrive ) ) {
				pingpong++;
				continue;
			}
			if ( !G_OAXNavFloor( arrive, 512.0f, end ) ) {
				G_OAXNavSkip( "teleporter", ent, "destination", navFloorWhy );
				continue;
			}
			G_OAXNavAdd( OAX_NAV_TELEPORT, start, end, G_OAXNavVolumeRadius( ent ), 0, i );
		}
	}
	return pingpong;
}

/*
=================
G_OAXNavFlyArc

Fly a player box from origin with velocity vel under the level's gravity,
in pmove-sized steps, sliding along walls, until it lands on walkable
ground. Returns 1 with the landing feet position.
=================
*/
static int G_OAXNavFlyArc( const vec3_t origin, const vec3_t vel, vec3_t feet ) {
	vec3_t p, v, next, mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	float dt = NAVLINK_SIM_MSEC * 0.001f, gravity = g_gravity.value;
	int t, bumps;
	trace_t tr;

	VectorCopy( origin, p );
	VectorCopy( vel, v );
	for ( t = 0; t < NAVLINK_SIM_TIME; t += NAVLINK_SIM_MSEC ) {
		float left = dt;
		v[2] -= gravity * dt;
		for ( bumps = 0; bumps < 4 && left > 0; bumps++ ) {
			VectorMA( p, left, v, next );
			trap_Trace( &tr, p, mins, maxs, next, ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( tr.allsolid ) {
				return 0;
			}
			VectorCopy( tr.endpos, p );
			if ( tr.fraction >= 1.0f ) {
				break;
			}
			if ( tr.plane.normal[2] >= OAX_MIN_WALK_NORMAL && v[2] <= 0 ) {
				VectorCopy( p, feet );
				feet[2] += mins[2];
				return 1;
			}
			left -= left * tr.fraction;
			{
				float d = DotProduct( v, tr.plane.normal );
				VectorMA( v, -d * 1.001f, tr.plane.normal, v );
			}
		}
	}
	return 0;
}

static void G_OAXNavJumpPads( void ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		vec3_t start, o, end;

		if ( !ent->inuse || ent->s.eType != ET_PUSH_TRIGGER || VectorLength( ent->s.origin2 ) <= 0 ) {
			continue;
		}
		if ( !G_OAXNavVolumeFloor( ent, start ) ) {
			G_OAXNavSkip( "jump pad", ent, "pad", navFloorWhy );
			continue;
		}
		/* a player standing on the pad's centre */
		VectorCopy( start, o );
		o[2] += 24.0f + 1.0f;
		if ( !G_OAXNavFlyArc( o, ent->s.origin2, end ) ) {
			G_OAXNavSkip( "jump pad", ent, "arc", "no landing within 4 s" );
			continue;
		}
		G_OAXNavAdd( OAX_NAV_JUMPPAD, start, end, G_OAXNavVolumeRadius( ent ), 0, i );
	}
}

static void G_OAXNavLadders( void ) {
	static const float dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	int i, d;

	for ( i = 0; i < MAX_OAX_ZONES; i++ ) {
		const bgOAXZone_t *z = &bg_oaxZones[i];
		vec3_t bottom, c, half, best;
		float bestZ = -1e30f;
		int found = 0;

		if ( !z->model || !z->active || z->ladder <= 0 ) {
			continue;
		}
		VectorAdd( z->absmin, z->absmax, c );
		VectorScale( c, 0.5f, c );
		VectorSubtract( z->absmax, c, half );
		{
			vec3_t o;
			VectorSet( o, c[0], c[1], z->absmin[2] + 32.0f );
			if ( !G_OAXNavFloor( o, 128.0f, bottom ) ) {
				continue;
			}
		}
		/* the walkable ground just beside the top of the volume */
		for ( d = 0; d < 4; d++ ) {
			vec3_t o, feet;
			o[0] = c[0] + dirs[d][0] * ( half[0] + 24.0f );
			o[1] = c[1] + dirs[d][1] * ( half[1] + 24.0f );
			o[2] = z->absmax[2] + 32.0f;
			if ( G_OAXNavFloor( o, 128.0f, feet ) && feet[2] > bottom[2] + 32.0f && feet[2] > bestZ ) {
				bestZ = feet[2];
				VectorCopy( feet, best );
				found = 1;
			}
		}
		if ( found ) {
			G_OAXNavAdd( OAX_NAV_LADDER, bottom, best, 32.0f, 0, z->entityNum );
		}
	}
}

/*QUAKED info_oax_route (0 .7 .3) (-8 -8 -8) (8 8 8)
One end of a navigation route for movement the game supports by rule; the
other end is its "target" (another info_oax_route).
"kind"    jump (default), drop, or translocator (only with g_oaxTranslocator 1)
"bidir"   1: usable both ways
"radius"  how far from the walkable surface an end may be (default 32)
*/
void SP_info_oax_route( gentity_t *ent ) {
	char *kind;
	float radius;
	int bidir;

	G_SpawnString( "kind", "jump", &kind );
	G_SpawnFloat( "radius", "32", &radius );
	G_SpawnInt( "bidir", "0", &bidir );
	if ( !Q_stricmp( kind, "translocator" ) ) {
		ent->count = OAX_NAV_TRANSLOCATOR;
	} else if ( !Q_stricmp( kind, "drop" ) ) {
		ent->count = OAX_NAV_DROP;
	} else {
		ent->count = OAX_NAV_JUMP;
	}
	ent->speed = radius;
	ent->damage = bidir;
	ent->r.svFlags |= SVF_NOCLIENT;
	G_SetOrigin( ent, ent->s.origin );
}

static void G_OAXNavRouteLinks( void ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i], *dest;
		vec3_t start, end, o;

		if ( !ent->inuse || !ent->classname || Q_stricmp( ent->classname, "info_oax_route" ) || !ent->target ) {
			continue;
		}
		dest = G_OAXNavNextTarget( NULL, ent->target );
		if ( !dest ) {
			G_OAXNavSkip( "route", ent, "target", "not found" );
			continue;
		}
		VectorCopy( ent->s.origin, o );
		o[2] += 24.0f;
		if ( !G_OAXNavFloor( o, 256.0f, start ) ) {
			G_OAXNavSkip( "route", ent, "start", navFloorWhy );
			continue;
		}
		VectorCopy( dest->s.origin, o );
		o[2] += 24.0f;
		if ( !G_OAXNavFloor( o, 256.0f, end ) ) {
			G_OAXNavSkip( "route", ent, "end", navFloorWhy );
			continue;
		}
		G_OAXNavAdd( ent->count, start, end, ent->speed, ent->damage, i );
	}
}

static int G_OAXNavHazards( void ) {
	int i, n = 0;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		float cost;

		if ( !ent->inuse || !ent->classname || Q_stricmp( ent->classname, "trigger_hurt" ) || !ent->r.linked ) {
			continue;
		}
		cost = NAVLINK_HAZARD_COST;
		if ( ent->damage >= 100 ) {
			cost = -1.0f;	/* kills on touch: off the mesh */
		}
		if ( navCostSet[i] ) {
			cost = navCost[i];
		}
		if ( trap_OAX_NavAddArea( ent->r.absmin, ent->r.absmax, cost ) >= 0 ) {
			n++;
		}
	}
	for ( i = 0; i < MAX_OAX_ZONES; i++ ) {
		const bgOAXZone_t *z = &bg_oaxZones[i];
		float cost = NAVLINK_HAZARD_COST;
		if ( !z->model || !z->active || z->damage <= 0 ) {
			continue;
		}
		if ( z->damage >= 100 ) {
			cost = -1.0f;	/* kills within a second: off the mesh, like a deadly trigger_hurt */
		}
		if ( navCostSet[z->entityNum] ) {
			cost = navCost[z->entityNum];
		}
		if ( trap_OAX_NavAddArea( z->absmin, z->absmax, cost ) >= 0 ) {
			n++;
		}
	}
	return n;
}

/* once the level has settled: every jump pad has aimed (AimAtTarget runs a frame after spawn) */
void G_OAXNavLinksFrame( void ) {
	int pingpong, hazards, polys;

	if ( navLinksBuilt || level.time < level.startTime + 300 ) {
		return;
	}
	navLinksBuilt = 1;
	if ( !G_OAXNavBotsActive() ) {
		return;
	}
	pingpong = G_OAXNavTeleporters();
	G_OAXNavJumpPads();
	G_OAXNavLadders();
	G_OAXNavRouteLinks();
	hazards = G_OAXNavHazards();
	polys = trap_OAX_NavCommit();
	BG_OAXDebugSetInt( "g_nav_links", numNavLinks );
	BG_OAXDebugSetInt( "g_nav_links_skipped_count", navNumSkipped );
	BG_OAXDebugSet( "g_nav_links_skipped", navSkipped[0] ? navSkipped : "-" );
	BG_OAXDebugSetInt( "g_nav_links_pingpong", pingpong );
	BG_OAXDebugSetInt( "g_nav_hazards", hazards );
	BG_OAXDebugSetInt( "g_nav_polys_intent", polys );
}
