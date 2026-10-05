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
g_oax_battle.c: battlefield entities for objective maps (Assault and any
other mode).

- target_oax_shake: when used, everyone's view shakes (or only near it).
  "intensity" degrees (3), "duration" seconds (1.5), "radius" (0 =
  everyone; else it fades out to that distance). The cgame does the shaking
  (server command oaxshake).
- target_oax_explosion: when used, explosions at its origin. "dmg" (0 =
  only the look of it), "radius" (160), "count" (1), "delay" seconds
  between them (0.2), "spread" units of scatter (0).
- trigger_oax_artillery (a brush volume): shells land on the players in
  it. Every "interval" seconds (3, varied by half) one player of the
  targeted side inside it is picked; a whistle, and "delay" seconds (1)
  later a shell lands within "spread" units (160) of where they were:
  "dmg" (80) in "radius" (200). "role" attack | defend | any: who it aims
  at in Assault (default attack; outside Assault, anyone). Assault's
  "after" / "until" switch it on and off with the stages.
- misc_oax_turret: an automatic heavy machine gun. It turns to the nearest
  player of the other side it can see within "range" (2000) and within
  "arc" degrees (180) of its "angle", and fires bursts: "dmg" (8) per round
  every "interval" ms (100). "role" defend | attack: whose turret it is in
  Assault (default defend; outside Assault it shoots at nobody unless
  "team" is red or blue, then at the other team). "health" (300; 0 =
  indestructible): only the other side's damage counts; it explodes and
  stays silent. "after" / "until" stage it like the other Assault keys.

Everything here is driven by level time and the game's own random numbers:
the same match plays the same on every build.
===========================================================================
*/
#include "g_local.h"
#include "bg_oax_assault.h"

/* debug values (tests): g_battle_shakes, _blasts, _shells, _turret_rounds, _turret_hits, _turrets_down */
static int	btShakes, btBlasts, btShells, btRounds, btHits, btDown;

void G_OAXBattleInit( void ) {
	btShakes = btBlasts = btShells = btRounds = btHits = btDown = 0;
}

static void OAX_BattlePublish( void ) {
	BG_OAXDebugSet( "g_battle", va( "%i %i %i %i %i %i", btShakes, btBlasts, btShells, btRounds, btHits, btDown ) );
}

/* ---- shake ---------------------------------------------------------------------------- */

static void Use_OAXShake( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	btShakes++;
	OAX_BattlePublish();
	trap_SendServerCommand( -1, va( "oaxshake %.2f %i %i %i %i %i", self->speed, (int)( self->wait * 1000.0f ),
		(int)self->s.origin[0], (int)self->s.origin[1], (int)self->s.origin[2], (int)self->random ) );
}

void SP_target_oax_shake( gentity_t *ent ) {
	G_SpawnFloat( "intensity", "3", &ent->speed );
	G_SpawnFloat( "duration", "1.5", &ent->wait );
	G_SpawnFloat( "radius", "0", &ent->random );
	ent->use = Use_OAXShake;
	ent->r.svFlags |= SVF_NOCLIENT;
}

/* ---- explosions ------------------------------------------------------------------------- */

static void OAX_Blast( gentity_t *owner, const vec3_t at, int dmg, float radius ) {
	gentity_t *te;
	vec3_t up = { 0, 0, 1 }, p;

	VectorCopy( at, p );
	te = G_TempEntity( p, EV_MISSILE_MISS );
	te->s.weapon = WP_ROCKET_LAUNCHER;
	te->s.eventParm = DirToByte( up );
	btBlasts++;
	OAX_BattlePublish();
	if ( dmg > 0 ) {
		G_RadiusDamage( p, owner, dmg, radius, NULL, MOD_ROCKET_SPLASH );
	}
}

static void Think_OAXExplosion( gentity_t *self ) {
	vec3_t p;
	int i;

	VectorCopy( self->s.origin, p );
	for ( i = 0; i < 2; i++ ) {
		p[i] += crandom() * self->random;
	}
	OAX_Blast( &g_entities[ENTITYNUM_WORLD], p, self->damage, self->speed );
	if ( --self->health > 0 ) {
		self->nextthink = level.time + (int)( self->wait * 1000.0f );
	}
}

static void Use_OAXExplosion( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	self->health = self->count > 0 ? self->count : 1;
	Think_OAXExplosion( self );
}

void SP_target_oax_explosion( gentity_t *ent ) {
	G_SpawnInt( "dmg", "0", &ent->damage );
	G_SpawnFloat( "radius", "160", &ent->speed );
	G_SpawnInt( "count", "1", &ent->count );
	G_SpawnFloat( "delay", "0.2", &ent->wait );
	G_SpawnFloat( "spread", "0", &ent->random );
	ent->use = Use_OAXExplosion;
	ent->think = Think_OAXExplosion;
	ent->r.svFlags |= SVF_NOCLIENT;
}

/* ---- artillery ------------------------------------------------------------------------------ */

/* who a battle entity aims at: in Assault its role's opponents (or the
   attackers / defenders a "role" names for artillery), else anyone */
static qboolean OAX_IsTarget( gentity_t *e, int targetRole ) {
	int att;

	if ( !e->inuse || !e->client || e->health <= 0 || e->client->sess.sessionTeam == TEAM_SPECTATOR ||
		e->client->ps.pm_type == PM_DEAD ) {
		return qfalse;
	}
	if ( !G_OAXAssaultActive() || targetRole < 0 ) {
		return qtrue;
	}
	att = G_OAXAssaultAttackers();
	return targetRole == 0 ? e->client->sess.sessionTeam == att : e->client->sess.sessionTeam != att;
}

static void Think_OAXArtillery( gentity_t *self ) {
	self->nextthink = level.time + 100;
	/* a shell on its way */
	if ( self->pain_debounce_time && level.time >= self->pain_debounce_time ) {
		trace_t tr;
		vec3_t down;
		self->pain_debounce_time = 0;
		VectorCopy( self->pos1, down );
		down[2] -= 2048;
		trap_Trace( &tr, self->pos1, NULL, NULL, down, ENTITYNUM_NONE, MASK_SOLID );
		OAX_Blast( &g_entities[ENTITYNUM_WORLD], tr.endpos, self->damage, self->speed );
	}
	if ( level.time < self->timestamp || !G_OAXAssaultStageOpen( self ) || ( G_OAXAssaultActive() && !G_OAXAssaultLive() ) ) {
		return;
	}
	/* the next one: a random player of the targeted side inside */
	self->timestamp = level.time + (int)( self->wait * 1000.0f * ( 0.5f + random() ) );
	{
		int touch[MAX_GENTITIES], n, i, picks[MAX_CLIENTS], np = 0;
		gentity_t *who;
		gentity_t *te;
		n = trap_EntitiesInBox( self->r.absmin, self->r.absmax, touch, MAX_GENTITIES );
		for ( i = 0; i < n; i++ ) {
			if ( touch[i] < MAX_CLIENTS && OAX_IsTarget( &g_entities[touch[i]], self->count ) ) {
				picks[np++] = touch[i];
			}
		}
		if ( !np ) {
			return;
		}
		who = &g_entities[picks[rand() % np]];
		VectorCopy( who->client->ps.origin, self->pos1 );
		self->pos1[0] += crandom() * self->random;
		self->pos1[1] += crandom() * self->random;
		self->pos1[2] += 64;
		self->pain_debounce_time = level.time + (int)( self->splashRadius > 0 ? self->splashRadius : 1000 );
		te = G_TempEntity( self->pos1, EV_GENERAL_SOUND );
		te->s.eventParm = self->noise_index;
		btShells++;
		OAX_BattlePublish();
	}
}

void SP_trigger_oax_artillery( gentity_t *ent ) {
	char *s;
	float delay;

	G_SpawnInt( "dmg", "80", &ent->damage );
	G_SpawnFloat( "radius", "200", &ent->speed );
	G_SpawnFloat( "interval", "3", &ent->wait );
	G_SpawnFloat( "spread", "160", &ent->random );
	G_SpawnFloat( "delay", "1", &delay );
	ent->splashRadius = (int)( delay * 1000.0f );	/* the shell's flight, ms */
	G_SpawnString( "role", "attack", &s );
	ent->count = !Q_stricmp( s, "defend" ) ? 1 : !Q_stricmp( s, "any" ) ? -1 : 0;
	ent->noise_index = G_SoundIndex( "sound/weapons/rocket/rockfly.wav" );
	trap_SetBrushModel( ent, ent->model );
	ent->r.contents = 0;
	ent->r.svFlags |= SVF_NOCLIENT;
	trap_LinkEntity( ent );
	ent->think = Think_OAXArtillery;
	ent->nextthink = level.time + 1000;
	ent->timestamp = level.time + 2000;
}

/* ---- the turret ----------------------------------------------------------------------------- */

#define TURRET_TURN		240.0f	/* degrees a second */
#define TURRET_HEIGHT	24.0f	/* the gun above its origin */

/* the side a turret shoots at: 0 attackers, 1 defenders, -1 nobody; outside
   Assault a team key picks the other team (TEAM_RED / TEAM_BLUE) */
static qboolean OAX_TurretEnemy( gentity_t *self, gentity_t *e ) {
	if ( !e->inuse || !e->client || e->health <= 0 || e->client->sess.sessionTeam == TEAM_SPECTATOR ||
		e->client->ps.pm_type == PM_DEAD ) {
		return qfalse;
	}
	if ( G_OAXAssaultActive() ) {
		int att = G_OAXAssaultAttackers();
		/* a defenders' turret (role 1) shoots attackers */
		return self->count == 1 ? e->client->sess.sessionTeam == att : e->client->sess.sessionTeam != att;
	}
	return self->s.generic1 && e->client->sess.sessionTeam != self->s.generic1;
}

static void Think_OAXTurret( gentity_t *self ) {
	vec3_t muzzle, wanted, best = { 0, 0, 0 }, fwd;
	float bestDist = self->speed, step;
	int i, target = -1;

	self->nextthink = level.time + 50;
	if ( self->health <= 0 && self->takedamage == qfalse && self->s.eFlags & EF_DEAD ) {
		return;
	}
	if ( !G_OAXAssaultStageOpen( self ) || ( G_OAXAssaultActive() && !G_OAXAssaultLive() ) ) {
		return;
	}
	VectorCopy( self->s.origin, muzzle );
	muzzle[2] += TURRET_HEIGHT;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *e = &g_entities[i];
		vec3_t at, d, ang;
		trace_t tr;
		float dist;
		if ( !OAX_TurretEnemy( self, e ) ) {
			continue;
		}
		VectorCopy( e->client->ps.origin, at );
		at[2] += 8;
		VectorSubtract( at, muzzle, d );
		dist = VectorLength( d );
		if ( dist >= bestDist ) {
			continue;
		}
		vectoangles( d, ang );
		if ( fabs( AngleSubtract( ang[YAW], self->pos2[YAW] ) ) > self->random ) {
			continue;
		}
		trap_Trace( &tr, muzzle, NULL, NULL, at, self->s.number, MASK_SHOT );
		if ( tr.entityNum != i ) {
			continue;
		}
		target = i;
		bestDist = dist;
		VectorCopy( ang, best );
	}
	if ( target < 0 ) {
		return;
	}
	/* turn toward it */
	step = TURRET_TURN * 0.05f;
	for ( i = 0; i < 2; i++ ) {
		float d = AngleSubtract( best[i], self->s.apos.trBase[i] );
		if ( d > step ) {
			d = step;
		} else if ( d < -step ) {
			d = -step;
		}
		self->s.apos.trBase[i] = AngleMod( self->s.apos.trBase[i] + d );
	}
	VectorCopy( self->s.apos.trBase, self->r.currentAngles );
	if ( fabs( AngleSubtract( best[YAW], self->s.apos.trBase[YAW] ) ) > 6.0f ||
		fabs( AngleSubtract( best[PITCH], self->s.apos.trBase[PITCH] ) ) > 6.0f ) {
		return;
	}
	/* fire the rounds due since the last think */
	while ( level.time >= self->timestamp ) {
		trace_t tr;
		vec3_t end, right, up, dir;
		gentity_t *hit, *te;
		self->timestamp = ( self->timestamp > level.time - 200 ? self->timestamp : level.time ) + self->splashRadius;
		AngleVectors( self->s.apos.trBase, fwd, right, up );
		VectorMA( muzzle, 8192, fwd, end );
		VectorMA( end, crandom() * 200, right, end );
		VectorMA( end, crandom() * 200, up, end );
		trap_Trace( &tr, muzzle, NULL, NULL, end, self->s.number, MASK_SHOT );
		self->s.powerups = ( self->s.powerups + 1 ) & 0xffff;
		btRounds++;
		if ( tr.surfaceFlags & SURF_NOIMPACT ) {
			continue;
		}
		hit = &g_entities[tr.entityNum];
		SnapVectorTowards( tr.endpos, muzzle );
		if ( hit->takedamage && hit->client ) {
			te = G_TempEntity( tr.endpos, EV_BULLET_HIT_FLESH );
			te->s.eventParm = hit->s.number;
		} else {
			te = G_TempEntity( tr.endpos, EV_BULLET_HIT_WALL );
			te->s.eventParm = DirToByte( tr.plane.normal );
		}
		te->s.otherEntityNum = self->s.number;
		if ( hit->takedamage ) {
			VectorSubtract( end, muzzle, dir );
			VectorNormalize( dir );
			G_Damage( hit, self, self, dir, tr.endpos, self->damage, 0, MOD_MACHINEGUN );
			if ( hit->client ) {
				btHits++;
			}
		}
	}
	OAX_BattlePublish();
	trap_LinkEntity( self );
}

/* for the bots: does this turret shoot at this player's side */
qboolean G_OAXTurretHostile( gentity_t *turret, gentity_t *player ) {
	return OAX_TurretEnemy( turret, player );
}

/* G_Damage: qtrue when targ is a turret (it took the hit, or its own side's) */
qboolean G_OAXTurretDamage( gentity_t *targ, gentity_t *attacker, int damage ) {
	if ( targ->s.eType != ET_OAX_TURRET ) {
		return qfalse;
	}
	if ( !targ->takedamage || !attacker || !attacker->client || !OAX_TurretEnemy( targ, attacker ) ) {
		return qtrue;
	}
	targ->health -= damage;
	if ( targ->health <= 0 ) {
		vec3_t p;
		VectorCopy( targ->s.origin, p );
		p[2] += TURRET_HEIGHT;
		OAX_Blast( targ, p, 0, 0 );
		targ->takedamage = qfalse;
		targ->s.eFlags |= EF_DEAD;
		targ->r.contents = 0;
		trap_LinkEntity( targ );
		AddScore( attacker, p, 2 );
		btDown++;
		OAX_BattlePublish();
	}
	return qtrue;
}

void SP_misc_oax_turret( gentity_t *ent ) {
	char *s;

	G_SpawnString( "role", "defend", &s );
	ent->count = !Q_stricmp( s, "attack" ) ? 0 : 1;
	G_SpawnString( "team", "", &s );
	ent->s.generic1 = !Q_stricmp( s, "red" ) ? TEAM_RED : !Q_stricmp( s, "blue" ) ? TEAM_BLUE : 0;
	G_SpawnFloat( "range", "2000", &ent->speed );
	G_SpawnFloat( "arc", "180", &ent->random );
	G_SpawnInt( "dmg", "8", &ent->damage );
	G_SpawnInt( "interval", "100", &ent->splashRadius );
	if ( ent->splashRadius < 30 ) {
		ent->splashRadius = 30;
	}
	G_SpawnInt( "health", "300", &ent->health );
	G_SpawnFloat( "angle", "0", &ent->pos2[YAW] );
	ent->s.eType = ET_OAX_TURRET;
	ent->s.modelindex = G_ModelIndex( "models/weapons/vulcan/vulcan.md3" );
	ent->s.apos.trType = TR_INTERPOLATE;
	VectorClear( ent->s.apos.trBase );
	ent->s.apos.trBase[YAW] = ent->pos2[YAW];
	ent->s.pos.trType = TR_STATIONARY;
	VectorCopy( ent->s.origin, ent->s.pos.trBase );
	VectorCopy( ent->s.origin, ent->r.currentOrigin );
	VectorSet( ent->r.mins, -16, -16, 0 );
	VectorSet( ent->r.maxs, 16, 16, TURRET_HEIGHT + 16 );
	if ( ent->health > 0 ) {
		ent->takedamage = qtrue;
		ent->r.contents = CONTENTS_SOLID;
	} else {
		ent->r.contents = CONTENTS_SOLID;
	}
	G_SoundIndex( "sound/weapons/vulcan/vulcanf1b.wav" );
	ent->think = Think_OAXTurret;
	ent->nextthink = level.time + 500;
	ent->timestamp = level.time;
	trap_LinkEntity( ent );
}
