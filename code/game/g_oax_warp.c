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
g_oax_warp.c: seamless warp zones (trigger_teleport spawnflag 4).

A SEAMLESS trigger_teleport is a thin volume around a plane. When a
player's origin or a missile crosses the plane (from the front to the
back, inside the trigger's face), it is moved by the rigid transform that
takes the trigger's frame to its target's (a misc_teleporter_dest):
position, velocity and view turn together, with no 400 ups kick and no
telefrag. The classname stays trigger_teleport so bspc still builds
teleporter reachabilities for bots.

Frames:
  source  center of the trigger's bounds; forward is the trigger's "angles"
          / "angle" key, else +X/+Y/+Z along its thinnest axis. Players
          crossing along forward are warped.
  target  the target's origin and "angles" / "angle": the source center
          lands on the target origin, source forward on target forward.

Put a misc_portal_surface on a wall a little behind the plane (a player's
box must fit between the plane and the wall) with its misc_portal_camera at
the same offset behind the target, to see through the warp.

Limits (v1): cgame does not predict the warp (one snapshot of correction,
hidden by EF_TELEPORT_BIT); spectators use it as a wall; hitscan stops at
the portal wall.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_sim.h"

#define MAX_OAX_WARPS 32

typedef struct {
	gentity_t *trig;
	int        ready;
	vec3_t     center;
	vec3_t     srcAngles, srcAxis[3];
	vec3_t     dstOrigin;
	vec3_t     dstAngles, dstAxis[3];
	int        yawOnly;     /* both frames level: views turn by an exact yaw */
} oaxWarp_t;

static oaxWarp_t warps[MAX_OAX_WARPS];
static int       numWarps;
static int       warpPlayerCount, warpMissileCount;

static void G_OAXWarpPoint( const oaxWarp_t *w, const vec3_t in, vec3_t out ) {
	vec3_t d;
	float  l0, l1, l2;

	VectorSubtract( in, w->center, d );
	l0 = DotProduct( d, w->srcAxis[0] );
	l1 = DotProduct( d, w->srcAxis[1] );
	l2 = DotProduct( d, w->srcAxis[2] );
	VectorMA( w->dstOrigin, l0, w->dstAxis[0], out );
	VectorMA( out, l1, w->dstAxis[1], out );
	VectorMA( out, l2, w->dstAxis[2], out );
}

static void G_OAXWarpDir( const oaxWarp_t *w, const vec3_t in, vec3_t out ) {
	float l0, l1, l2;

	l0 = DotProduct( in, w->srcAxis[0] );
	l1 = DotProduct( in, w->srcAxis[1] );
	l2 = DotProduct( in, w->srcAxis[2] );
	VectorScale( w->dstAxis[0], l0, out );
	VectorMA( out, l1, w->dstAxis[1], out );
	VectorMA( out, l2, w->dstAxis[2], out );
}

/* a -> b crosses the plane front to back inside the trigger's face */
static int G_OAXWarpCrosses( const oaxWarp_t *w, const vec3_t a, const vec3_t b, vec3_t hit ) {
	vec3_t d;
	float  da, db, f;
	int    i;

	if ( !w->ready ) {
		return 0;
	}
	VectorSubtract( a, w->center, d );
	da = DotProduct( d, w->srcAxis[0] );
	VectorSubtract( b, w->center, d );
	db = DotProduct( d, w->srcAxis[0] );
	if ( !( da < 0 && db >= 0 ) ) {
		return 0;
	}
	f = da / ( da - db );
	for ( i = 0; i < 3; i++ ) {
		hit[i] = a[i] + f * ( b[i] - a[i] );
		if ( hit[i] < w->trig->r.absmin[i] || hit[i] > w->trig->r.absmax[i] ) {
			return 0;
		}
	}
	return 1;
}

static void G_OAXWarpPlayer( oaxWarp_t *w, gentity_t *ent ) {
	playerState_t *ps = &ent->client->ps;
	vec3_t         origin, vel, angles;
	float          before;

	before = VectorLength( ps->velocity );
	G_OAXWarpPoint( w, ps->origin, origin );
	G_OAXWarpDir( w, ps->velocity, vel );

	VectorCopy( ps->viewangles, angles );
	if ( w->yawOnly ) {
		angles[YAW] += w->dstAngles[YAW] - w->srcAngles[YAW];
	} else {
		vec3_t fwd, f2;
		AngleVectors( ps->viewangles, fwd, NULL, NULL );
		G_OAXWarpDir( w, fwd, f2 );
		vectoangles( f2, angles );
	}

	VectorCopy( origin, ps->origin );
	VectorCopy( vel, ps->velocity );
	SetClientViewAngle( ent, angles );
	VectorCopy( origin, ent->client->oldOrigin );

	// no lerp across the jump; no kick, no telefrag
	ps->eFlags ^= EF_TELEPORT_BIT;
	G_ResetHistory( ent );
	BG_PlayerStateToEntityState( ps, &ent->s, qtrue );
	VectorCopy( ps->origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );

	warpPlayerCount++;
	BG_OAXDebugSetInt( "g_warp_count", warpPlayerCount );
	BG_OAXDebugSet( "g_warp_last", va( "%i %f %f %f %f", ps->commandTime, before, VectorLength( ps->velocity ),
		w->srcAngles[YAW], ps->viewangles[YAW] ) );
}

static void G_OAXWarpTouch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	oaxWarp_t *w = &warps[self->count];
	vec3_t     hit;

	if ( !other->client || other->client->ps.pm_type != PM_NORMAL ) {
		return;
	}
	if ( other->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	if ( !G_OAXWarpCrosses( w, other->client->oldOrigin, other->client->ps.origin, hit ) ) {
		return;
	}
	G_OAXWarpPlayer( w, other );
}

static void G_OAXWarpLocate( gentity_t *self ) {
	oaxWarp_t *w = &warps[self->count];
	gentity_t *dest;

	dest = G_PickTarget( self->target );
	if ( !dest ) {
		G_Printf( "seamless trigger_teleport: no target %s\n", self->target ? self->target : "" );
		return;
	}
	VectorCopy( dest->s.origin, w->dstOrigin );
	VectorCopy( dest->s.angles, w->dstAngles );
	AnglesToAxis( w->dstAngles, w->dstAxis );
	w->yawOnly = w->srcAngles[PITCH] == 0 && w->srcAngles[ROLL] == 0 &&
	             w->dstAngles[PITCH] == 0 && w->dstAngles[ROLL] == 0;
	w->ready = 1;
}

/*
=================
G_OAXWarpSpawn

SP_trigger_teleport hands a SEAMLESS (4) trigger here after InitTrigger.
=================
*/
void G_OAXWarpSpawn( gentity_t *self ) {
	oaxWarp_t *w;
	vec3_t     size;
	float      yaw;
	int        thin, i;

	if ( numWarps >= MAX_OAX_WARPS ) {
		G_Printf( "seamless trigger_teleport: more than %i, ignored\n", MAX_OAX_WARPS );
		return;
	}
	w = &warps[numWarps];
	memset( w, 0, sizeof( *w ) );
	w->trig = self;
	self->count = numWarps++;

	for ( i = 0; i < 3; i++ ) {
		w->center[i] = 0.5f * ( self->r.absmin[i] + self->r.absmax[i] );
		size[i] = self->r.absmax[i] - self->r.absmin[i];
	}
	if ( G_SpawnVector( "angles", "0 0 0", w->srcAngles ) ) {
		/* explicit frame */
	} else if ( G_SpawnFloat( "angle", "0", &yaw ) ) {
		VectorClear( w->srcAngles );
		w->srcAngles[YAW] = yaw;
	} else {
		vec3_t fwd;
		thin = 0;
		for ( i = 1; i < 3; i++ ) {
			if ( size[i] < size[thin] ) {
				thin = i;
			}
		}
		VectorClear( fwd );
		fwd[thin] = 1;
		vectoangles( fwd, w->srcAngles );
	}
	AnglesToAxis( w->srcAngles, w->srcAxis );

	// the cgame never needs it (no hyperspace effect for a seamless warp)
	self->r.svFlags |= SVF_NOCLIENT;
	self->touch = G_OAXWarpTouch;
	self->think = G_OAXWarpLocate;
	self->nextthink = level.time + FRAMETIME;
	trap_LinkEntity( self );
}

/*
=================
G_OAXWarpMissile

G_RunMissile calls this with the missile's new position before it traces.
If the move crosses a warp plane with nothing in the way, the missile's
trajectory continues from the transformed crossing point.
=================
*/
void G_OAXWarpMissile( gentity_t *ent, vec3_t origin ) {
	int        i;
	vec3_t     hit, vel, newCur, newOrigin;
	trace_t    tr;
	oaxWarp_t *w;

	for ( i = 0; i < numWarps; i++ ) {
		w = &warps[i];
		if ( !G_OAXWarpCrosses( w, ent->r.currentOrigin, origin, hit ) ) {
			continue;
		}
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, hit, ent->r.ownerNum, ent->clipmask );
		if ( tr.fraction < 1 || tr.startsolid ) {
			return;     /* it hits something first */
		}
		BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, vel );
		G_OAXWarpDir( w, vel, ent->s.pos.trDelta );
		G_OAXWarpPoint( w, hit, newCur );
		G_OAXWarpPoint( w, origin, newOrigin );
		VectorCopy( newOrigin, ent->s.pos.trBase );
		ent->s.pos.trTime = level.time;
		VectorCopy( newCur, ent->r.currentOrigin );
		VectorCopy( newOrigin, origin );

		warpMissileCount++;
		BG_OAXDebugSetInt( "g_warp_missiles", warpMissileCount );
		/* whole units: bg_lib's %.1f can cut a string short */
		BG_OAXDebugSet( "g_warp_missile", va( "%i %i %i %i %i %i %i", level.time,
			(int)newOrigin[0], (int)newOrigin[1], (int)newOrigin[2],
			(int)ent->s.pos.trDelta[0], (int)ent->s.pos.trDelta[1], (int)ent->s.pos.trDelta[2] ) );
		return;
	}
}
