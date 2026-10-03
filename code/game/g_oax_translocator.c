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
g_oax_translocator.c: the translocator rule (classic
play stays available, the translocator is a server rule).

g_oaxTranslocator 1 (server info, latched; default 0) gives every player
a translocator in the grappling hook's slot (it replaces the hook while
the rule is on):

- Fire with no beacon out: throws the beacon from the muzzle at a fixed
  speed along the view (no spread, no randomness). It flies under the
  level's gravity, bounces off walls at half speed and stops dead on the
  first walkable surface it hits, so where it lands is a function of the
  throw alone.
- Fire with the beacon out: the thrower teleports to it (standing on the
  spot it marks; refused if a player box does not fit there). Velocity is
  cleared on arrival.
- Telefrag rules:
  - arriving on someone telefrags them (G_KillBox, as teleporters do);
  - a beacon an enemy has damaged is disrupted: translocating to it kills
    the thrower (credited to the disruptor, MOD_TELEFRAG);
  - a flag carrier who translocates drops the flag.
- The beacon is gone when its owner dies, respawns or disconnects, when it
  falls into a nodrop volume, or after 20 s.

With the rule on, navmesh bots may use "translocator" route links
(info_oax_route kind translocator, g_oax_navlinks.c).

Debug values: g_tl_throws, g_tl_ports, g_tl_telefrags, g_tl_disrupted.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_nav.h"

#define TL_SPEED		900.0f
#define TL_LIFETIME		20000
#define TL_SIZE			6.0f

static vmCvar_t		g_oaxTranslocator;
static gentity_t	*tlBeacon[MAX_CLIENTS];
static int			tlThrows, tlPorts, tlDisrupted, tlTelefrags, tlFireCalls;

void G_OAXTranslocatorInit( void ) {
	trap_Cvar_Register( &g_oaxTranslocator, "g_oaxTranslocator", "0", CVAR_SERVERINFO | CVAR_LATCH );
	memset( tlBeacon, 0, sizeof( tlBeacon ) );
	tlThrows = tlPorts = tlDisrupted = tlTelefrags = tlFireCalls = 0;
	BG_OAXDebugSetInt( "g_tl_rule", g_oaxTranslocator.integer );
}

int G_OAXTranslocatorActive( void ) {
	return g_oaxTranslocator.integer != 0;
}

float G_OAXTranslocatorSpeed( void ) {
	return TL_SPEED;
}

gentity_t *G_OAXTranslocatorBeacon( int clientNum ) {
	gentity_t *b;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return NULL;
	}
	b = tlBeacon[clientNum];
	if ( !b || !b->inuse || b->r.ownerNum != clientNum || Q_stricmp( b->classname, "oax_beacon" ) ) {
		tlBeacon[clientNum] = NULL;
		return NULL;
	}
	return b;
}

void G_OAXTranslocatorFree( gentity_t *owner ) {
	gentity_t *b;

	if ( !owner || !owner->client ) {
		return;
	}
	b = G_OAXTranslocatorBeacon( owner->s.number );
	if ( b ) {
		G_FreeEntity( b );
	}
	tlBeacon[owner->s.number] = NULL;
}

void G_OAXTranslocatorSpawn( gentity_t *ent ) {
	G_OAXTranslocatorFree( ent );
	if ( !G_OAXTranslocatorActive() ) {
		return;
	}
	ent->client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_GRAPPLING_HOOK );
	ent->client->ps.ammo[WP_GRAPPLING_HOOK] = -1;
}

static void G_OAXBeaconPain( gentity_t *self, gentity_t *attacker, int damage ) {
	gentity_t *owner = &g_entities[self->r.ownerNum];

	self->health = 100000;
	if ( !attacker || !attacker->client || attacker == owner ) {
		return;
	}
	if ( g_gametype.integer >= GT_TEAM && OnSameTeam( attacker, owner ) ) {
		return;
	}
	if ( !self->enemy ) {
		tlDisrupted++;
		BG_OAXDebugSetInt( "g_tl_disrupted", tlDisrupted );
	}
	self->enemy = attacker;
}

static void G_OAXBeaconThink( gentity_t *self ) {
	gentity_t *owner = &g_entities[self->r.ownerNum];
	vec3_t pos;
	trace_t tr;

	self->nextthink = level.time + 1;
	if ( !owner->inuse || !owner->client || owner->client->pers.connected != CON_CONNECTED || owner->health <= 0 ||
		level.time - self->count > TL_LIFETIME ) {
		tlBeacon[self->r.ownerNum] = NULL;
		G_FreeEntity( self );
		return;
	}
	if ( self->s.pos.trType == TR_STATIONARY ) {
		return;
	}
	BG_EvaluateTrajectory( &self->s.pos, level.time, pos );
	trap_Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, pos, self->s.number, MASK_SOLID );
	if ( tr.startsolid ) {
		tr.fraction = 0;
		VectorCopy( self->r.currentOrigin, tr.endpos );
		VectorSet( tr.plane.normal, 0, 0, 1 );
	}
	VectorCopy( tr.endpos, self->r.currentOrigin );
	if ( tr.fraction < 1.0f ) {
		if ( tr.plane.normal[2] >= OAX_MIN_WALK_NORMAL ) {
			G_SetOrigin( self, tr.endpos );
		} else {
			vec3_t vel;
			float dot;
			int hitTime = level.previousTime + ( level.time - level.previousTime ) * tr.fraction;
			BG_EvaluateTrajectoryDelta( &self->s.pos, hitTime, vel );
			dot = DotProduct( vel, tr.plane.normal );
			VectorMA( vel, -2 * dot, tr.plane.normal, vel );
			VectorScale( vel, 0.5f, vel );
			VectorCopy( tr.endpos, self->s.pos.trBase );
			VectorCopy( vel, self->s.pos.trDelta );
			self->s.pos.trTime = level.time;
		}
	}
	if ( trap_PointContents( self->r.currentOrigin, -1 ) & CONTENTS_NODROP ) {
		tlBeacon[self->r.ownerNum] = NULL;
		G_FreeEntity( self );
		return;
	}
	trap_LinkEntity( self );
}

static void G_OAXTranslocatorThrow( gentity_t *ent, vec3_t muzzle, vec3_t forward ) {
	gentity_t *b = G_Spawn();

	b->classname = "oax_beacon";
	b->s.eType = ET_GENERAL;
	b->s.modelindex = G_ModelIndex( "models/powerups/holdable/teleporter.md3" );
	b->r.ownerNum = ent->s.number;
	b->parent = ent;
	VectorSet( b->r.mins, -TL_SIZE, -TL_SIZE, -TL_SIZE );
	VectorSet( b->r.maxs, TL_SIZE, TL_SIZE, TL_SIZE );
	b->r.contents = CONTENTS_CORPSE;	/* shots hit it (MASK_SHOT); players walk through */
	b->takedamage = qtrue;
	b->health = 100000;
	b->pain = G_OAXBeaconPain;
	b->count = level.time;
	b->s.pos.trType = TR_GRAVITY;
	b->s.pos.trTime = level.time;
	VectorCopy( muzzle, b->s.pos.trBase );
	VectorScale( forward, TL_SPEED, b->s.pos.trDelta );
	SnapVector( b->s.pos.trDelta );
	VectorCopy( muzzle, b->r.currentOrigin );
	b->think = G_OAXBeaconThink;
	b->nextthink = level.time + 1;
	trap_LinkEntity( b );
	tlBeacon[ent->s.number] = b;
	tlThrows++;
	BG_OAXDebugSetInt( "g_tl_throws", tlThrows );
}

static void G_OAXTranslocatorDropFlag( gentity_t *ent ) {
	int pw[3] = { PW_REDFLAG, PW_BLUEFLAG, PW_NEUTRALFLAG };
	int i;

	for ( i = 0; i < 3; i++ ) {
		if ( ent->client->ps.powerups[pw[i]] ) {
			gitem_t *item = BG_FindItemForPowerup( pw[i] );
			if ( item ) {
				Drop_Item( ent, item, 0 );
			}
			ent->client->ps.powerups[pw[i]] = 0;
		}
	}
}

static void G_OAXTranslocatorPort( gentity_t *ent, gentity_t *b ) {
	vec3_t spot, angles, pmins = { -15, -15, -24 }, pmaxs = { 15, 15, 32 };
	trace_t tr;
	int k, ok = 0;

	/* stand on the spot the beacon marks: feet at its bottom */
	for ( k = 0; k < 3 && !ok; k++ ) {
		VectorCopy( b->r.currentOrigin, spot );
		spot[2] += -TL_SIZE + 24.0f + 1.0f + k * 16.0f;
		trap_Trace( &tr, spot, pmins, pmaxs, spot, ent->s.number, MASK_SOLID | CONTENTS_PLAYERCLIP );
		ok = !tr.startsolid && !tr.allsolid;
	}
	if ( !ok ) {
		return;		/* no room: the beacon stays */
	}
	if ( b->enemy && b->enemy->inuse ) {
		gentity_t *disruptor = b->enemy;
		tlBeacon[ent->s.number] = NULL;
		G_FreeEntity( b );
		G_Damage( ent, disruptor, disruptor, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_TELEFRAG );
		return;
	}
	tlBeacon[ent->s.number] = NULL;
	G_FreeEntity( b );
	G_OAXTranslocatorDropFlag( ent );
	{
		/* who is about to be telefragged (G_KillBox in TeleportPlayer) */
		int touch[MAX_GENTITIES], num, i;
		vec3_t mins, maxs;
		VectorAdd( spot, ent->r.mins, mins );
		VectorAdd( spot, ent->r.maxs, maxs );
		num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
		for ( i = 0; i < num; i++ ) {
			gentity_t *hit = &g_entities[touch[i]];
			if ( hit != ent && hit->client && hit->health > 0 && hit->client->sess.sessionTeam != TEAM_SPECTATOR ) {
				tlTelefrags++;
			}
		}
	}
	VectorClear( ent->client->ps.velocity );
	VectorSet( angles, 1e7f, 0, 0 );	/* keep the view (TeleportPlayer's noAngles) */
	TeleportPlayer( ent, spot, angles );
	tlPorts++;
	BG_OAXDebugSetInt( "g_tl_ports", tlPorts );
	BG_OAXDebugSetInt( "g_tl_telefrags", tlTelefrags );
}

/* FireWeapon for WP_GRAPPLING_HOOK: qtrue when the rule took the shot */
qboolean G_OAXTranslocatorFire( gentity_t *ent, vec3_t muzzle, vec3_t forward ) {
	gentity_t *b;

	if ( !G_OAXTranslocatorActive() || !ent->client ) {
		return qfalse;
	}
	tlFireCalls++;
	BG_OAXDebugSetInt( "g_tl_fire_calls", tlFireCalls );
	b = G_OAXTranslocatorBeacon( ent->s.number );
	if ( b ) {
		G_OAXTranslocatorPort( ent, b );
	} else {
		G_OAXTranslocatorThrow( ent, muzzle, forward );
	}
	return qtrue;
}

/*
=================
G_OAXTranslocatorPredict

Where a beacon thrown from start with velocity vel comes to rest, flown
the way G_OAXBeaconThink flies it (chords of the parabola per server
frame, bounces, a stop on walkable ground). For bots choosing a throw.
Returns 1 with the resting centre.
=================
*/
int G_OAXTranslocatorPredict( const vec3_t start, const vec3_t vel, int frameMsec, vec3_t out ) {
	trajectory_t tj;
	vec3_t prev, pos, mins, maxs;
	int t, k;
	trace_t tr;

	if ( frameMsec <= 0 ) {
		frameMsec = 50;
	}
	VectorSet( mins, -TL_SIZE, -TL_SIZE, -TL_SIZE );
	VectorSet( maxs, TL_SIZE, TL_SIZE, TL_SIZE );
	memset( &tj, 0, sizeof( tj ) );
	tj.trType = TR_GRAVITY;
	tj.trTime = 0;
	VectorCopy( start, tj.trBase );
	VectorCopy( vel, tj.trDelta );
	SnapVector( tj.trDelta );
	VectorCopy( start, prev );
	for ( t = frameMsec, k = 0; t <= 4000 && k < 400; t += frameMsec, k++ ) {
		BG_EvaluateTrajectory( &tj, t, pos );
		trap_Trace( &tr, prev, mins, maxs, pos, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.startsolid ) {
			VectorCopy( prev, out );
			return 1;
		}
		VectorCopy( tr.endpos, prev );
		if ( tr.fraction < 1.0f ) {
			vec3_t v;
			float dot;
			int hitTime;
			if ( tr.plane.normal[2] >= OAX_MIN_WALK_NORMAL ) {
				VectorCopy( tr.endpos, out );
				return 1;
			}
			hitTime = t - frameMsec + (int)( frameMsec * tr.fraction );
			BG_EvaluateTrajectoryDelta( &tj, hitTime, v );
			dot = DotProduct( v, tr.plane.normal );
			VectorMA( v, -2 * dot, tr.plane.normal, v );
			VectorScale( v, 0.5f, v );
			VectorCopy( tr.endpos, tj.trBase );
			VectorCopy( v, tj.trDelta );
			tj.trTime = t;
		}
	}
	return 0;
}
