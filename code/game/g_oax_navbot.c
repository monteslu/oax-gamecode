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
g_oax_navbot.c: bots that path on the engine's navigation mesh (see
g_oax_nav.h), for maps without AAS.

A compact bot brain, run from BotAIStartFrame in place of the AAS bots:

- goals: in CTF a bot carrying the enemy flag runs home; attackers go for
  the enemy flag (or a dropped one, or escort a teammate who has it);
  defenders chase whoever carries their flag or return it when dropped,
  and otherwise sweep the map's items. In other modes every bot sweeps the
  items: it heads for the nearest item nobody has visited lately, so every
  pickup gets visited during a match.
- movement: the navmesh's straight path (corner points), re-pathed every
  1.5 s or when the goal moves; stuck detection jumps and strafes, then
  gives the goal up.
- combat: the nearest visible enemy, aimed at with a skill-dependent error
  that changes every 300 ms, fired at when the aim is close; best weapon
  with ammo.
- links (navigation from intent, g_oax_navlinks.c): a path corner that
  starts an off-mesh link hands the bot to a small per-kind routine:
  teleporters and jump pads are walked into, ladders climbed, jump routes
  jumped, drops walked off, and translocator routes (rule
  g_oaxTranslocator) thrown and ported: the beacon is aimed with the
  ballistic solution the beacon itself flies (G_OAXTranslocatorPredict).
  Any teleport ends the link and re-paths.

Everything is a function of the game state and a per-bot LCG, never a wall
clock, so a match plays the same on every build.
===========================================================================
*/
#include "g_local.h"
#include "oax_public.h"
#include "g_oax_nav.h"
#include "g_oax_vehicle.h"
#include "bg_oax_assault.h"

#define NB_MAX_POINTS	32
#define NB_REPATH_MS	1500
#define NB_STUCK_MS		1000
#define NB_GOAL_MS		500
#define NB_ENEMY_MS		100
#define NB_SIGHT		3000.0f

enum {
	NBG_NONE,
	NBG_ITEM,			/* sweep: goalEnt is an item */
	NBG_ENEMY_FLAG,		/* enemy flag at its base or dropped */
	NBG_HOME,			/* carrying the enemy flag: our base */
	NBG_CARRIER,		/* a client: enemy carrier to kill, or teammate to escort */
	NBG_OWN_FLAG,		/* our dropped flag: touch it to return it */
	NBG_OBJECTIVE,		/* Assault attacker: the first open objective */
	NBG_GUARD			/* Assault defender: a defender spawn spot by the objectives */
};

typedef struct {
	int			inuse;
	int			skill;
	unsigned	rng;
	vec3_t		view;
	int			lastTime;
	usercmd_t	cmd;
	/* goal */
	int			goalKind;
	int			goalEnt;
	vec3_t		goal;
	int			goalTime;
	/* path */
	float		pts[NB_MAX_POINTS * 3];
	int			numPts;
	int			cur;
	int			pathTime;
	vec3_t		pathGoal;
	int			links[NB_MAX_POINTS];	/* off-mesh link starting at each point, -1 none */
	/* the off-mesh link being taken */
	int			link;
	int			linkKind;
	int			linkTime;
	int			linkPhase;
	int			linkPhaseTime;
	int			linkAir;
	int			linkFails;
	int			teleBit;
	/* stuck */
	vec3_t		lastPos;
	int			lastCheck;
	int			stuck;
	int			unstickUntil;
	int			strafe;
	/* combat */
	int			enemy;
	int			enemyTime;
	float		aimErr[2];
	int			aimTime;
	int			wasDead;
	/* Assault: the objective this bot attacks (type, centre, entity, use radius) */
	int			objType;
	vec3_t		objPoint;
	int			objEnt;
	float		objRadius;
} navBot_t;

static navBot_t	navBots[MAX_CLIENTS];
static byte		nbVisited[MAX_GENTITIES];				/* item visited in this sweep round */
static byte		nbFailed[MAX_CLIENTS][MAX_GENTITIES];	/* this bot found no way to the item */
static int		nbPaths, nbNoPath, nbGoalsReached, nbStuck;
static int		nbLinksTaken, nbLinksDone, nbLinksFailed;
static int		nbLinkKindsDone;	/* OR of the link kinds completed */
static int		nbStuckAt[8][4];	/* x y z goal kind of the last stuck events */
static int		nbKindStats[16][3];
static int		nbTlAttack;	/* by kind bit: taken, done, failed */
static char		nbLinkFail[16][80];	/* by kind bit: the last failure */

static int NB_KindSlot( int kind ) {
	int i;
	for ( i = 0; i < 16; i++ ) {
		if ( kind == ( 1 << i ) ) {
			return i;
		}
	}
	return 15;
}
static unsigned	nbHash = 2166136261U;
static int		nbNextHashTime;
static vmCvar_t	bot_oaxNav;		/* 0: steer straight at goals, no navmesh paths (a test control) */
static vmCvar_t	bot_oaxIdle;	/* 1: bots stand still and hold fire (a test target), but still respawn */

static float NB_Rand( navBot_t *nb ) {
	nb->rng = nb->rng * 1103515245U + 12345U;
	return (float)( ( nb->rng >> 8 ) & 0xffffff ) / 16777216.0f;
}

void G_OAXNavBotInit( void ) {
	trap_Cvar_Register( &bot_oaxNav, "bot_oaxNav", "1", 0 );
	trap_Cvar_Register( &bot_oaxIdle, "bot_oaxIdle", "0", 0 );
	memset( navBots, 0, sizeof( navBots ) );
	memset( nbVisited, 0, sizeof( nbVisited ) );
	memset( nbFailed, 0, sizeof( nbFailed ) );
	nbPaths = nbNoPath = nbGoalsReached = nbStuck = 0;
	memset( nbStuckAt, 0, sizeof( nbStuckAt ) );
	nbLinksTaken = nbLinksDone = nbLinksFailed = nbLinkKindsDone = 0;
	memset( nbKindStats, 0, sizeof( nbKindStats ) );
	memset( nbLinkFail, 0, sizeof( nbLinkFail ) );
	nbHash = 2166136261U;
	nbNextHashTime = 0;
}

int G_OAXNavBotsActive( void ) {
	/* Assault is played by these bots even where the map has AAS (the
	   stock bots know nothing of objectives; the engine builds the
	   navmesh for g_gametype 14) */
	return ( !trap_AAS_Initialized() || g_gametype.integer == GT_ASSAULT ) && BG_OAXFeature( "nav" ) && trap_OAX_NavStatus() > 0;
}

qboolean G_OAXNavBotConnect( int clientNum, float skill ) {
	navBot_t *nb;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !G_OAXNavBotsActive() ) {
		return qfalse;
	}
	nb = &navBots[clientNum];
	memset( nb, 0, sizeof( *nb ) );
	memset( nbFailed[clientNum], 0, sizeof( nbFailed[clientNum] ) );
	nb->inuse = 1;
	nb->skill = (int)skill;
	if ( nb->skill < 1 ) {
		nb->skill = 1;
	}
	if ( nb->skill > 5 ) {
		nb->skill = 5;
	}
	nb->rng = 0x9e3779b9U ^ ( (unsigned)clientNum * 2654435761U );
	nb->enemy = -1;
	nb->goalEnt = -1;
	nb->link = -1;
	return qtrue;
}

static float HorizDist( const vec3_t a, const vec3_t b ) {
	float dx = a[0] - b[0], dy = a[1] - b[1];
	return sqrt( dx * dx + dy * dy );
}

/* ---- goals -------------------------------------------------------------------- */

static qboolean NB_IsSweepItem( gentity_t *e ) {
	return e->inuse && e->item && !( e->flags & FL_DROPPED_ITEM ) && e->item->giType != IT_TEAM;
}

/* the base flag entity (never the dropped one) of a flag powerup */
static gentity_t *NB_BaseFlag( int pw ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->item && e->item->giType == IT_TEAM && e->item->giTag == pw && !( e->flags & FL_DROPPED_ITEM ) ) {
			return e;
		}
	}
	return NULL;
}

static gentity_t *NB_DroppedFlag( int pw ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->item && e->item->giType == IT_TEAM && e->item->giTag == pw && ( e->flags & FL_DROPPED_ITEM ) ) {
			return e;
		}
	}
	return NULL;
}

static gentity_t *NB_Carrier( int pw ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->client && e->client->pers.connected == CON_CONNECTED && e->health > 0 &&
			e->client->ps.powerups[pw] ) {
			return e;
		}
	}
	return NULL;
}

/* this bot's rank among its team's bots, and how many there are */
static int NB_TeamRank( int clientNum, int *count ) {
	int i, rank = 0, n = 0, team = g_entities[clientNum].client->sess.sessionTeam;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !e->inuse || !e->client || !navBots[i].inuse || e->client->sess.sessionTeam != team ) {
			continue;
		}
		if ( i < clientNum ) {
			rank++;
		}
		n++;
	}
	*count = n;
	return rank;
}

static void NB_SetGoal( navBot_t *nb, int kind, int ent, const vec3_t pos ) {
	if ( kind != nb->goalKind || ent != nb->goalEnt ) {
		nb->stuck = 0;
	}
	nb->goalKind = kind;
	nb->goalEnt = ent;
	VectorCopy( pos, nb->goal );
}

/* nearest unvisited item this bot can reach; a new round when all were visited */
static void NB_SweepGoal( int clientNum, navBot_t *nb, const vec3_t origin ) {
	int i, best = -1, round;
	float bestDist = 0;

	if ( nb->goalKind == NBG_ITEM && nb->goalEnt >= 0 && !nbVisited[nb->goalEnt] && !nbFailed[clientNum][nb->goalEnt] ) {
		return;
	}
	for ( round = 0; round < 2 && best < 0; round++ ) {
		for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
			gentity_t *e = &g_entities[i];
			float d;
			if ( !NB_IsSweepItem( e ) || nbVisited[i] || nbFailed[clientNum][i] ) {
				continue;
			}
			d = Distance( origin, e->r.currentOrigin );
			if ( best < 0 || d < bestDist ) {
				best = i;
				bestDist = d;
			}
		}
		if ( best < 0 ) {
			/* everything visited (or failed): a new round */
			memset( nbVisited, 0, sizeof( nbVisited ) );
			if ( round == 1 ) {
				memset( nbFailed[clientNum], 0, sizeof( nbFailed[clientNum] ) );
			}
		}
	}
	if ( best >= 0 ) {
		NB_SetGoal( nb, NBG_ITEM, best, g_entities[best].r.currentOrigin );
	} else {
		NB_SetGoal( nb, NBG_NONE, -1, origin );
	}
}

/* Assault: attackers go for the first open objective, defenders hold the
   defender spawn spots by the objectives (spread by their rank) */
static qboolean NB_AssaultGoal( int clientNum, navBot_t *nb, gentity_t *ent ) {
	gclient_t *cl = ent->client;
	vec3_t point;
	gentity_t *oent = NULL;
	int type, count, rank;
	float radius;

	if ( !G_OAXAssaultActive() || ( cl->sess.sessionTeam != TEAM_RED && cl->sess.sessionTeam != TEAM_BLUE ) ) {
		return qfalse;
	}
	if ( cl->sess.sessionTeam == G_OAXAssaultAttackers() ) {
		if ( !G_OAXAssaultObjective( 0, point, &type, &oent, &radius ) ) {
			return qfalse;
		}
		nb->objType = type;
		nb->objEnt = oent ? oent->s.number : -1;
		nb->objRadius = radius;
		VectorCopy( point, nb->objPoint );
		NB_SetGoal( nb, NBG_OBJECTIVE, nb->objEnt, point );
		return qtrue;
	}
	rank = NB_TeamRank( clientNum, &count );
	if ( !G_OAXAssaultGuardSpot( rank, point ) ) {
		return qfalse;
	}
	nb->objType = -1;
	NB_SetGoal( nb, NBG_GUARD, -1, point );
	return qtrue;
}

static void NB_ChooseGoal( int clientNum, navBot_t *nb, gentity_t *ent ) {
	gclient_t *cl = ent->client;
	vec3_t origin;

	VectorCopy( cl->ps.origin, origin );
	if ( g_gametype.integer == GT_ASSAULT ) {
		if ( NB_AssaultGoal( clientNum, nb, ent ) ) {
			return;
		}
		nb->objType = -1;
	}
	if ( g_gametype.integer == GT_CTF &&
		( cl->sess.sessionTeam == TEAM_RED || cl->sess.sessionTeam == TEAM_BLUE ) ) {
		int ownPw = cl->sess.sessionTeam == TEAM_RED ? PW_REDFLAG : PW_BLUEFLAG;
		int enemyPw = cl->sess.sessionTeam == TEAM_RED ? PW_BLUEFLAG : PW_REDFLAG;
		gentity_t *home = NB_BaseFlag( ownPw ), *target, *carrier;
		int count, rank = NB_TeamRank( clientNum, &count );
		qboolean defender = count >= 2 && rank == count - 1;	/* the last bot of a team of two or more */

		if ( cl->ps.powerups[enemyPw] && home ) {
			NB_SetGoal( nb, NBG_HOME, home->s.number, home->s.origin );
			return;
		}
		/* our flag: a carrier to kill, or a dropped flag to return */
		carrier = NB_Carrier( ownPw );
		target = NB_DroppedFlag( ownPw );
		if ( defender || carrier || target ) {
			if ( carrier && ( defender || Distance( origin, carrier->r.currentOrigin ) < 1500.0f ) ) {
				NB_SetGoal( nb, NBG_CARRIER, carrier->s.number, carrier->r.currentOrigin );
				return;
			}
			if ( target && ( defender || Distance( origin, target->r.currentOrigin ) < 1500.0f ) ) {
				NB_SetGoal( nb, NBG_OWN_FLAG, target->s.number, target->r.currentOrigin );
				return;
			}
		}
		if ( !defender ) {
			carrier = NB_Carrier( enemyPw );
			if ( carrier && carrier != ent ) {
				/* a teammate has it: escort */
				NB_SetGoal( nb, NBG_CARRIER, carrier->s.number, carrier->r.currentOrigin );
				return;
			}
			target = NB_DroppedFlag( enemyPw );
			if ( !target ) {
				target = NB_BaseFlag( enemyPw );
			}
			if ( target ) {
				NB_SetGoal( nb, NBG_ENEMY_FLAG, target->s.number, target->r.currentOrigin );
				return;
			}
		}
	}
	NB_SweepGoal( clientNum, nb, origin );
}

/* ---- perception and combat ------------------------------------------------------ */

static int NB_FindEnemy( gentity_t *ent ) {
	int i, best = -1;
	float bestDist = NB_SIGHT;
	vec3_t eye;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *o = &g_entities[i];
		vec3_t oe;
		trace_t tr;
		float d;

		if ( o == ent || !o->inuse || !o->client || o->client->pers.connected != CON_CONNECTED || o->health <= 0 ||
			o->client->sess.sessionTeam == TEAM_SPECTATOR || ( g_gametype.integer >= GT_TEAM && OnSameTeam( ent, o ) ) ) {
			continue;
		}
		VectorCopy( o->client->ps.origin, oe );
		oe[2] += o->client->ps.viewheight;
		d = Distance( eye, oe );
		if ( d >= bestDist ) {
			continue;
		}
		trap_Trace( &tr, eye, NULL, NULL, oe, ent->s.number, MASK_SHOT );
		/* a driver sits inside its vehicle: the vehicle in the way counts as seeing it */
		if ( tr.fraction >= 1.0f || tr.entityNum == i || ( tr.entityNum < ENTITYNUM_MAX_NORMAL && tr.entityNum == G_OAXVehicleOfClient( i, NULL ) ) ) {
			best = i;
			bestDist = d;
		}
	}
	/* turrets that shoot at this bot's side (misc_oax_turret) */
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *o = &g_entities[i];
		vec3_t oe;
		trace_t tr;
		float d;
		if ( !o->inuse || o->s.eType != ET_OAX_TURRET || !o->takedamage || !G_OAXTurretHostile( o, ent ) ) {
			continue;
		}
		VectorCopy( o->s.origin, oe );
		oe[2] += 24;
		d = Distance( eye, oe );
		if ( d >= bestDist ) {
			continue;
		}
		trap_Trace( &tr, eye, NULL, NULL, oe, ent->s.number, MASK_SHOT );
		if ( tr.entityNum == i ) {
			best = i;
			bestDist = d;
		}
	}
	return best;
}

static const int nbWeaponOrder[] = {
	WP_ROCKET_LAUNCHER, WP_RAILGUN, WP_LIGHTNING, WP_PLASMAGUN, WP_SHOTGUN,
	WP_MACHINEGUN, WP_GRENADE_LAUNCHER, WP_GAUNTLET
};

static int NB_BestWeapon( gclient_t *cl ) {
	int i;

	for ( i = 0; i < (int)( sizeof( nbWeaponOrder ) / sizeof( nbWeaponOrder[0] ) ); i++ ) {
		int w = nbWeaponOrder[i];
		if ( ( cl->ps.stats[STAT_WEAPONS] & ( 1 << w ) ) && ( cl->ps.ammo[w] != 0 || w == WP_GAUNTLET ) ) {
			return w;
		}
	}
	return cl->ps.weapon;
}

/* turn the view toward wanted angles at most maxStep degrees per axis */
static void NB_Turn( navBot_t *nb, const vec3_t wanted, float maxStep ) {
	int i;

	for ( i = 0; i < 2; i++ ) {
		float d = AngleSubtract( wanted[i], nb->view[i] );
		if ( d > maxStep ) {
			d = maxStep;
		} else if ( d < -maxStep ) {
			d = -maxStep;
		}
		nb->view[i] = AngleMod( nb->view[i] + d );
	}
	nb->view[ROLL] = 0;
}

/* ---- path following --------------------------------------------------------------- */

static void NB_FailGoal( int clientNum, navBot_t *nb ) {
	if ( nb->goalKind == NBG_ITEM && nb->goalEnt >= 0 ) {
		nbFailed[clientNum][nb->goalEnt] = 1;
	}
	nb->goalKind = NBG_NONE;
	nb->goalEnt = -1;
	nb->numPts = 0;
	nb->link = -1;
	nb->linkFails = 0;
}

static void NB_Repath( int clientNum, navBot_t *nb, const vec3_t origin, int time ) {
	vec3_t feet;
	int flags = 0;

	VectorCopy( origin, feet );
	feet[2] += MINS_Z;
	if ( !bot_oaxNav.integer ) {
		/* control: no navmesh, straight at the goal */
		VectorCopy( feet, nb->pts );
		VectorCopy( nb->goal, &nb->pts[3] );
		nb->links[0] = nb->links[1] = -1;
		nb->numPts = 2;
		nb->cur = 1;
		nb->link = -1;
		nb->pathTime = time;
		VectorCopy( nb->goal, nb->pathGoal );
		return;
	}
	/* Assault: teleporters closed to this bot's side stay out of its path */
	nb->numPts = trap_OAX_NavFindPathEx( feet, nb->goal, nb->pts, nb->links, NB_MAX_POINTS, &flags, G_OAXNavInclude(),
		G_OAXAssaultNavExclude( g_entities[clientNum].client->sess.sessionTeam ) );
	nb->cur = ( nb->numPts > 0 && nb->links[0] >= 0 ) ? 0 : 1;
	nb->link = -1;
	nb->pathTime = time;
	VectorCopy( nb->goal, nb->pathGoal );
	nbPaths++;
	if ( nb->numPts <= 0 || ( ( flags & OAX_NAV_PATH_PARTIAL ) && nb->goalKind == NBG_ITEM ) ) {
		nbNoPath++;
		NB_FailGoal( clientNum, nb );
	}
}

/* ---- off-mesh links ------------------------------------------------------------------ */

typedef struct {
	vec3_t	target;		/* steer toward (horizontally); unset when hasTarget is 0 */
	int		hasTarget;
	int		jump;
	int		attack;
	int		weapon;		/* 0: keep */
	int		ownView;	/* the link aims the view (no combat aiming) */
	vec3_t	view;
} nbLinkOut_t;

static void NB_LinkEnd( navBot_t *nb, int ok ) {
	nbKindStats[NB_KindSlot( nb->linkKind )][ok ? 1 : 2]++;
	if ( ok ) {
		nbLinksDone++;
		nbLinkKindsDone |= nb->linkKind;
		nb->linkFails = 0;
	} else {
		gentity_t *e = &g_entities[nb - navBots];
		nbLinksFailed++;
		nb->linkFails++;
		Com_sprintf( nbLinkFail[NB_KindSlot( nb->linkKind )], sizeof( nbLinkFail[0] ), "link %i phase %i after %i ms at %i %i %i w%i/%i",
			nb->link, nb->linkPhase, level.time - nb->linkTime, (int)e->client->ps.origin[0], (int)e->client->ps.origin[1], (int)e->client->ps.origin[2], e->client->ps.weapon, e->client->ps.weaponstate );
	}
	nb->link = -1;
	nb->numPts = 0;		/* re-path from where the link left us */
}

static void NB_LinkBegin( navBot_t *nb, int link, int time ) {
	const oaxNavLinkInfo_t *l = G_OAXNavLink( link );

	if ( !l ) {
		return;
	}
	nb->link = link;
	nb->linkKind = l->kind;
	nb->linkTime = time;
	nb->linkPhase = 0;
	nb->linkPhaseTime = time;
	nb->linkAir = 0;
	nbLinksTaken++;
	nbKindStats[NB_KindSlot( l->kind )][0]++;
}

/* view angles that throw the beacon from eye to land at target (low arc when it gets there) */
static void NB_ThrowAngles( const vec3_t eye, const vec3_t target, vec3_t angles ) {
	float v = G_OAXTranslocatorSpeed(), g = g_gravity.value, dx, dy, d, h, disc, th[2];
	int k, best = 1;

	dx = target[0] - eye[0];
	dy = target[1] - eye[1];
	d = sqrt( dx * dx + dy * dy );
	h = target[2] - eye[2];
	angles[YAW] = (float)( atan2( dy, dx ) * 180.0 / M_PI );
	angles[ROLL] = 0;
	if ( d < 1.0f ) {
		angles[PITCH] = -89.0f;
		return;
	}
	disc = v * v * v * v - g * ( g * d * d + 2.0f * h * v * v );
	if ( disc < 0 ) {
		angles[PITCH] = -45.0f;
		return;
	}
	th[0] = (float)atan2( v * v - sqrt( disc ), g * d );
	th[1] = (float)atan2( v * v + sqrt( disc ), g * d );
	for ( k = 0; k < 2; k++ ) {
		vec3_t a, fwd, rest, muzzle;
		VectorSet( a, (float)( -th[k] * 180.0 / M_PI ), angles[YAW], 0 );
		AngleVectors( a, fwd, NULL, NULL );
		VectorMA( eye, 14, fwd, muzzle );
		VectorScale( fwd, v, fwd );
		if ( G_OAXTranslocatorPredict( muzzle, fwd, 50, rest ) && Distance( rest, target ) < 64.0f ) {
			best = k;
			break;
		}
	}
	angles[PITCH] = (float)( -th[best] * 180.0 / M_PI );
}

/* one frame of taking link nb->link; fills out, ends the link when done or failed */
static void NB_LinkStep( navBot_t *nb, gentity_t *ent, int time, nbLinkOut_t *out ) {
	const oaxNavLinkInfo_t *l = G_OAXNavLink( nb->link );
	gclient_t *cl = ent->client;
	vec3_t origin, feet;
	int onGround = cl->ps.groundEntityNum != ENTITYNUM_NONE, elapsed = time - nb->linkTime;

	memset( out, 0, sizeof( *out ) );
	if ( !l ) {
		NB_LinkEnd( nb, 0 );
		return;
	}
	VectorCopy( cl->ps.origin, origin );
	VectorCopy( origin, feet );
	feet[2] += MINS_Z;
	if ( !onGround ) {
		nb->linkAir = 1;
	}
	switch ( l->kind ) {
	case OAX_NAV_TELEPORT:
		/* walk into the trigger; the teleport itself ends the link (NB_Think) */
		VectorCopy( l->start, out->target );
		out->hasTarget = 1;
		if ( elapsed > 3000 ) {
			NB_LinkEnd( nb, 0 );
		}
		return;

	case OAX_NAV_JUMPPAD:
		if ( nb->linkAir ) {
			VectorCopy( l->end, out->target );
		} else {
			VectorCopy( l->start, out->target );
		}
		out->hasTarget = 1;
		if ( nb->linkAir && onGround ) {
			NB_LinkEnd( nb, HorizDist( origin, l->end ) < 96.0f );
		} else if ( elapsed > 5000 ) {
			NB_LinkEnd( nb, 0 );
		}
		return;

	case OAX_NAV_LADDER:
		VectorCopy( l->end, out->target );
		out->hasTarget = 1;
		out->ownView = 1;
		out->view[PITCH] = 0;
		out->view[YAW] = (float)( atan2( l->end[1] - origin[1], l->end[0] - origin[0] ) * 180.0 / M_PI );
		if ( onGround && feet[2] > l->end[2] - 8.0f ) {
			NB_LinkEnd( nb, 1 );
		} else if ( elapsed > 6000 ) {
			NB_LinkEnd( nb, 0 );
		}
		return;

	case OAX_NAV_JUMP:
	case OAX_NAV_DROP:
		VectorCopy( l->end, out->target );
		out->hasTarget = 1;
		if ( l->kind == OAX_NAV_JUMP && nb->linkPhase == 0 && onGround ) {
			nb->linkPhase = 1;
			nb->linkPhaseTime = time;
		}
		/* hold jump until off the ground (a one-think press can miss a game frame) */
		if ( l->kind == OAX_NAV_JUMP && nb->linkPhase == 1 && onGround && time - nb->linkPhaseTime < 300 ) {
			out->jump = 1;
		}
		if ( nb->linkAir && onGround && time - nb->linkPhaseTime > 150 ) {
			/* a jump must arrive; a drop has done its job once it came down */
			NB_LinkEnd( nb, l->kind == OAX_NAV_DROP ? feet[2] < l->start[2] - 24.0f : Distance( feet, l->end ) < 64.0f );
		} else if ( l->kind == OAX_NAV_DROP && onGround && HorizDist( origin, l->end ) < 24.0f ) {
			NB_LinkEnd( nb, 1 );
		} else if ( elapsed > 3000 ) {
			NB_LinkEnd( nb, 0 );
		}
		return;

	case OAX_NAV_TRANSLOCATOR: {
		vec3_t eye, target;
		gentity_t *b = G_OAXTranslocatorBeacon( ent->s.number );

		if ( !G_OAXTranslocatorActive() || !( cl->ps.stats[STAT_WEAPONS] & ( 1 << WP_GRAPPLING_HOOK ) ) || elapsed > 8000 ) {
			NB_LinkEnd( nb, 0 );
			return;
		}
		out->weapon = WP_GRAPPLING_HOOK;
		out->ownView = 1;
		VectorCopy( origin, eye );
		eye[2] += cl->ps.viewheight;
		VectorCopy( l->end, target );
		target[2] += 6.0f;		/* the beacon's centre when it rests there */
		NB_ThrowAngles( eye, target, out->view );
		if ( nb->linkPhase == 0 ) {
			/* aimed, holding the translocator and ready: throw (or port to an old beacon) */
			if ( cl->ps.weapon == WP_GRAPPLING_HOOK && cl->ps.weaponstate == WEAPON_READY && cl->ps.weaponTime <= 0 &&
				fabs( AngleSubtract( out->view[YAW], nb->view[YAW] ) ) < 1.0f &&
				fabs( AngleSubtract( out->view[PITCH], nb->view[PITCH] ) ) < 1.0f ) {
				out->attack = 1;
				BG_OAXDebugSetInt( "g_navbot_tl_attack", ++nbTlAttack );
				nb->linkPhase = b ? 2 : 3;
				nb->linkPhaseTime = time;
			}
		} else if ( nb->linkPhase == 3 ) {
			/* throwing: hold fire until the beacon is out. Bots think every client
			   frame but the game runs its own frames, so a one-think press can be
			   overwritten before the server runs it */
			if ( b ) {
				nb->linkPhase = 1;
				nb->linkPhaseTime = time;
			} else if ( time - nb->linkPhaseTime > 300 ) {
				NB_LinkEnd( nb, 0 );
			} else {
				out->attack = 1;
			}
		} else if ( nb->linkPhase == 1 ) {
			/* the beacon flies: wait until it rests */
			if ( !b && time - nb->linkPhaseTime > 200 ) {
				NB_LinkEnd( nb, 0 );
			} else if ( b && ( b->s.pos.trType == TR_STATIONARY || time - nb->linkPhaseTime > 2500 ) && cl->ps.weaponTime <= 0 ) {
				nb->linkPhase = 2;
				nb->linkPhaseTime = time;
			}
		} else {
			/* port; the teleport ends the link (NB_Think) */
			out->attack = ( ( time - nb->linkPhaseTime ) / 50 ) & 1 ? 0 : 1;
			if ( time - nb->linkPhaseTime > 1500 ) {
				NB_LinkEnd( nb, 0 );
			}
		}
		return;
	}
	}
	NB_LinkEnd( nb, 0 );
}

/* ---- the per-frame brain ----------------------------------------------------------- */

static void NB_Think( int clientNum, int time ) {
	navBot_t *nb = &navBots[clientNum];
	gentity_t *ent = &g_entities[clientNum];
	gclient_t *cl = ent->client;
	vec3_t origin, dir, wanted, fwd, right;
	float msec, maxTurn, len;
	int j, mvx = 0, mvy = 0;
	nbLinkOut_t lo;
	qboolean fighting = qfalse, objShoot = qfalse, objUsing = qfalse;

	msec = nb->lastTime ? (float)( time - nb->lastTime ) : 50.0f;
	if ( msec <= 0 ) {
		msec = 1;
	}
	nb->lastTime = time;
	memset( &nb->cmd, 0, sizeof( nb->cmd ) );
	nb->cmd.serverTime = time;
	nb->cmd.weapon = cl->ps.weapon;

	if ( cl->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	if ( cl->ps.pm_type == PM_DEAD || ent->health <= 0 ) {
		/* tap fire to respawn */
		nb->cmd.buttons = ( ( time / 100 ) & 1 ) ? BUTTON_ATTACK : 0;
		nb->numPts = 0;
		nb->link = -1;
		nb->wasDead = 1;
		nb->goalKind = NBG_NONE;
		goto angles;
	}
	if ( nb->wasDead ) {
		nb->wasDead = 0;
		VectorCopy( cl->ps.viewangles, nb->view );
		VectorCopy( cl->ps.origin, nb->lastPos );
		nb->lastCheck = time;
	}
	VectorCopy( cl->ps.origin, origin );

	/* any teleport (teleporter, translocator, a test's setviewpos) ends a link and re-paths */
	if ( ( cl->ps.eFlags & EF_TELEPORT_BIT ) != nb->teleBit ) {
		nb->teleBit = cl->ps.eFlags & EF_TELEPORT_BIT;
		if ( nb->link >= 0 ) {
			NB_LinkEnd( nb, nb->linkKind == OAX_NAV_TELEPORT || nb->linkKind == OAX_NAV_TRANSLOCATOR );
		}
		nb->numPts = 0;
	}

	/* items someone is standing at (or just took) count as visited */
	for ( j = MAX_CLIENTS; j < level.num_entities; j++ ) {
		gentity_t *e = &g_entities[j];
		if ( !NB_IsSweepItem( e ) || nbVisited[j] ) {
			continue;
		}
		if ( ( e->r.svFlags & SVF_NOCLIENT ) ||
			( HorizDist( origin, e->r.currentOrigin ) < 40.0f && fabs( origin[2] - e->r.currentOrigin[2] ) < 64.0f ) ) {
			nbVisited[j] = 1;
		}
	}

	/* perception */
	if ( time - nb->enemyTime >= NB_ENEMY_MS ) {
		nb->enemyTime = time;
		nb->enemy = NB_FindEnemy( ent );
	}

	/* goal */
	if ( nb->goalKind == NBG_NONE || time - nb->goalTime >= NB_GOAL_MS ) {
		nb->goalTime = time;
		NB_ChooseGoal( clientNum, nb, ent );
	}
	if ( nb->goalKind != NBG_NONE && nb->goalKind != NBG_OBJECTIVE && nb->goalKind != NBG_GUARD &&
		HorizDist( origin, nb->goal ) < 32.0f && fabs( origin[2] + MINS_Z - nb->goal[2] ) < 64.0f ) {
		nbGoalsReached++;
		if ( nb->goalKind == NBG_ITEM ) {
			nbVisited[nb->goalEnt] = 1;
		}
		nb->goalKind = NBG_NONE;
		NB_ChooseGoal( clientNum, nb, ent );
	}

	/* path */
	if ( nb->goalKind != NBG_NONE && nb->link < 0 && nb->linkFails >= 3 ) {
		NB_FailGoal( clientNum, nb );
	}
	if ( nb->goalKind != NBG_NONE && nb->link < 0 &&
		( nb->numPts <= 0 || time - nb->pathTime >= NB_REPATH_MS || Distance( nb->goal, nb->pathGoal ) > 64.0f ) ) {
		NB_Repath( clientNum, nb, origin, time );
	}
	VectorClear( dir );
	memset( &lo, 0, sizeof( lo ) );
	if ( nb->goalKind != NBG_NONE && nb->numPts > 0 && nb->link < 0 ) {
		float *p;
		while ( nb->cur < nb->numPts - 1 && nb->links[nb->cur] < 0 && HorizDist( origin, &nb->pts[nb->cur * 3] ) < 40.0f ) {
			nb->cur++;
		}
		if ( nb->cur >= nb->numPts ) {
			nb->cur = nb->numPts - 1;
		}
		p = &nb->pts[nb->cur * 3];
		if ( nb->links[nb->cur] >= 0 ) {
			const oaxNavLinkInfo_t *l = G_OAXNavLink( nb->links[nb->cur] );
			/* at the link's start: within its radius (a trigger is touched from its edge) */
			if ( l && HorizDist( origin, p ) < ( l->radius > 32.0f ? l->radius : 32.0f ) + 16.0f && fabs( origin[2] + MINS_Z - p[2] ) < 40.0f ) {
				NB_LinkBegin( nb, nb->links[nb->cur], time );
			}
		}
		dir[0] = p[0] - origin[0];
		dir[1] = p[1] - origin[1];
	}
	if ( nb->link >= 0 ) {
		NB_LinkStep( nb, ent, time, &lo );
		VectorClear( dir );
		if ( lo.hasTarget ) {
			dir[0] = lo.target[0] - origin[0];
			dir[1] = lo.target[1] - origin[1];
		}
	}
	/* Assault: in range of the objective, stop and work it; a guard holds its spot */
	if ( nb->link < 0 && nb->goalKind == NBG_OBJECTIVE ) {
		float d = Distance( origin, nb->objPoint );
		if ( nb->objType == OAX_ASO_USE && d < nb->objRadius + 48.0f ) {
			VectorClear( dir );
			nb->cmd.buttons |= BUTTON_USE_HOLDABLE;
			objUsing = qtrue;
		} else if ( nb->objType == OAX_ASO_DESTROY && d < 700.0f && nb->objEnt >= 0 ) {
			trace_t tr;
			vec3_t eye;
			VectorCopy( origin, eye );
			eye[2] += cl->ps.viewheight;
			trap_Trace( &tr, eye, NULL, NULL, nb->objPoint, clientNum, MASK_SHOT );
			if ( tr.entityNum == nb->objEnt ) {
				objShoot = qtrue;
				if ( d < 450.0f ) {
					VectorClear( dir );
				}
			}
		}
	} else if ( nb->link < 0 && nb->goalKind == NBG_GUARD && HorizDist( origin, nb->goal ) < 48.0f ) {
		VectorClear( dir );
	}
	len = sqrt( dir[0] * dir[0] + dir[1] * dir[1] );
	if ( len > 0.001f ) {
		dir[0] /= len;
		dir[1] /= len;
	}

	/* stuck: little progress for a second while having somewhere to go (links time out on their own) */
	if ( time - nb->lastCheck >= NB_STUCK_MS ) {
		if ( nb->goalKind != NBG_NONE && nb->link < 0 && len > 0.001f && Distance( origin, nb->lastPos ) < 32.0f ) {
			nbStuck++;
			/* where (the last eight, for finding what traps bots) */
			nbStuckAt[nbStuck & 7][0] = (int)origin[0];
			nbStuckAt[nbStuck & 7][1] = (int)origin[1];
			nbStuckAt[nbStuck & 7][2] = (int)origin[2];
			nbStuckAt[nbStuck & 7][3] = nb->goalKind;
			if ( !bot_oaxNav.integer ) {
				/* control: pure straight-line steering, no recovery */
				goto stuckDone;
			}
			nb->stuck++;
			nb->unstickUntil = time + 600;
			nb->strafe = NB_Rand( nb ) < 0.5f ? -1 : 1;
			nb->numPts = 0;
			if ( nb->stuck >= 4 ) {
				NB_FailGoal( clientNum, nb );
				nb->stuck = 0;
			}
		} else {
			nb->stuck = 0;
		}
stuckDone:
		VectorCopy( origin, nb->lastPos );
		nb->lastCheck = time;
	}

	/* view: the enemy when one is in sight, else the way we are going */
	maxTurn = 540.0f * msec / 1000.0f;
	if ( lo.ownView ) {
		NB_Turn( nb, lo.view, maxTurn );
	} else if ( nb->enemy >= 0 ) {
		gentity_t *o = &g_entities[nb->enemy];
		vec3_t eye, aim;
		if ( time - nb->aimTime >= 300 ) {
			float err = ( 6 - nb->skill ) * 2.5f;
			nb->aimTime = time;
			nb->aimErr[0] = ( NB_Rand( nb ) - 0.5f ) * err;
			nb->aimErr[1] = ( NB_Rand( nb ) - 0.5f ) * err;
		}
		VectorCopy( origin, eye );
		eye[2] += cl->ps.viewheight;
		if ( o->client ) {
			VectorCopy( o->client->ps.origin, aim );
			aim[2] += 8;
		} else {
			VectorCopy( o->s.origin, aim );		/* a turret */
			aim[2] += 24;
		}
		VectorSubtract( aim, eye, aim );
		vectoangles( aim, wanted );
		wanted[PITCH] += nb->aimErr[0];
		wanted[YAW] += nb->aimErr[1];
		NB_Turn( nb, wanted, maxTurn );
		fighting = qtrue;
		nb->cmd.weapon = NB_BestWeapon( cl );
		if ( fabs( AngleSubtract( wanted[YAW], nb->view[YAW] ) ) < 12.0f &&
			fabs( AngleSubtract( wanted[PITCH], nb->view[PITCH] ) ) < 12.0f && cl->ps.weapon == nb->cmd.weapon ) {
			nb->cmd.buttons |= BUTTON_ATTACK;
		}
	} else if ( objShoot ) {
		vec3_t eye, aim;
		VectorCopy( origin, eye );
		eye[2] += cl->ps.viewheight;
		VectorSubtract( nb->objPoint, eye, aim );
		vectoangles( aim, wanted );
		NB_Turn( nb, wanted, maxTurn );
		nb->cmd.weapon = NB_BestWeapon( cl );
		if ( fabs( AngleSubtract( wanted[YAW], nb->view[YAW] ) ) < 10.0f &&
			fabs( AngleSubtract( wanted[PITCH], nb->view[PITCH] ) ) < 10.0f && cl->ps.weapon == nb->cmd.weapon ) {
			nb->cmd.buttons |= BUTTON_ATTACK;
		}
	} else if ( len > 0.001f ) {
		wanted[PITCH] = 0;
		wanted[YAW] = (float)( atan2( dir[1], dir[0] ) * 180.0 / M_PI );
		wanted[ROLL] = 0;
		NB_Turn( nb, wanted, maxTurn );
	}

	/* movement relative to the view's yaw */
	if ( len > 0.001f ) {
		vec3_t yawOnly;
		VectorSet( yawOnly, 0, nb->view[YAW], 0 );
		AngleVectors( yawOnly, fwd, right, NULL );
		mvx = (int)( ( dir[0] * fwd[0] + dir[1] * fwd[1] ) * 127.0f );
		mvy = (int)( ( dir[0] * right[0] + dir[1] * right[1] ) * 127.0f );
	} else if ( fighting && !objUsing ) {
		/* no goal: circle-strafe the enemy */
		mvy = ( ( time / 1000 ) & 1 ) ? 127 : -127;
	}
	if ( time < nb->unstickUntil ) {
		mvy = nb->strafe * 127;
		if ( nb->unstickUntil - time > 450 ) {
			nb->cmd.upmove = 127;
		}
	}
	if ( lo.jump ) {
		nb->cmd.upmove = 127;
	}
	if ( lo.weapon ) {
		nb->cmd.weapon = lo.weapon;
		nb->cmd.buttons &= ~BUTTON_ATTACK;
	}
	if ( lo.attack ) {
		nb->cmd.buttons |= BUTTON_ATTACK;
	}
	nb->cmd.forwardmove = (signed char)( mvx > 127 ? 127 : mvx < -127 ? -127 : mvx );
	nb->cmd.rightmove = (signed char)( mvy > 127 ? 127 : mvy < -127 ? -127 : mvy );

angles:
	for ( j = 0; j < 3; j++ ) {
		nb->cmd.angles[j] = ANGLE2SHORT( nb->view[j] ) - cl->ps.delta_angles[j];
	}
}

static void NB_Hash( int time ) {
	int i, k;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !navBots[i].inuse || !e->inuse || !e->client ) {
			continue;
		}
		for ( k = 0; k < 3; k++ ) {
			int v = (int)( e->client->ps.origin[k] * 8.0f );
			nbHash = ( nbHash ^ (unsigned)v ) * 16777619U;
		}
		nbHash = ( nbHash ^ (unsigned)e->health ) * 16777619U;
	}
	/* a checkpoint every 10 s of level time, so builds can be compared */
	if ( level.time >= nbNextHashTime && nbNextHashTime <= 300000 ) {
		BG_OAXDebugSet( va( "g_navbot_hash_%i", nbNextHashTime / 1000 ), va( "%08x", nbHash ) );
		nbNextHashTime += 10000;
	}
}

void G_OAXNavBotFrame( int time ) {
	int i, n = 0, guardN = 0, guardNear = 0, attackN = 0;
	float guardSum = 0;
	char cmdBuf[1024];

	trap_Cvar_Update( &bot_oaxNav );
	trap_Cvar_Update( &bot_oaxIdle );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( !navBots[i].inuse ) {
			continue;
		}
		if ( !ent->inuse || !ent->client || !( ent->r.svFlags & SVF_BOT ) ) {
			navBots[i].inuse = 0;
			continue;
		}
		if ( ent->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		/* drain the reliable commands sent to the bot, as BotAI does, or they overflow and drop it */
		while ( trap_BotGetServerCommand( i, cmdBuf, sizeof( cmdBuf ) ) ) {
		}
		NB_Think( i, time );
		if ( bot_oaxIdle.integer ) {
			/* a test target: keep only the respawn tap and the view */
			navBots[i].cmd.forwardmove = navBots[i].cmd.rightmove = navBots[i].cmd.upmove = 0;
			if ( ent->health > 0 ) {
				navBots[i].cmd.buttons = 0;
			}
		}
		/* Assault: how many defending bots are at their posts, how far on average */
		if ( level.framenum % 10 == 0 && navBots[i].goalKind == NBG_GUARD && ent->health > 0 ) {
			float d = Distance( ent->client->ps.origin, navBots[i].goal );
			guardN++;
			guardSum += d;
			if ( d < 300.0f ) {
				guardNear++;
			}
		}
		if ( navBots[i].goalKind == NBG_OBJECTIVE && ent->health > 0 ) {
			attackN++;
		}
		if ( i < 4 && level.framenum % 10 == 0 ) {
			BG_OAXDebugSet( va( "g_navbot_pos_%i", i ), va( "%.1f %.1f %.1f %i", ent->client->ps.origin[0], ent->client->ps.origin[1],
				ent->client->ps.origin[2], ent->health ) );
		}
		/* oax vehicles: boarding and driving toward the same goal (g_oax_vehbot.c) */
		G_OAXVehBotCommand( i, &navBots[i].cmd, navBots[i].goal, navBots[i].goalKind != NBG_NONE, time );
		trap_BotUserCommand( i, &navBots[i].cmd );
		n++;
	}
	NB_Hash( time );
	if ( level.framenum % 10 == 0 ) {
		BG_OAXDebugSetInt( "g_navbots", n );
		if ( g_gametype.integer == GT_ASSAULT ) {
			/* attackers on an objective, defenders posted, within 300 of the post, mean distance */
			BG_OAXDebugSet( "g_navbot_assault", va( "%i %i %i %.0f", attackN, guardN, guardNear, guardN ? guardSum / guardN : 0 ) );
		}
		BG_OAXDebugSetInt( "g_navbot_paths", nbPaths );
		BG_OAXDebugSetInt( "g_navbot_nopath", nbNoPath );
		BG_OAXDebugSetInt( "g_navbot_goals", nbGoalsReached );
		BG_OAXDebugSetInt( "g_navbot_stuck", nbStuck );
		{
			char buf[256] = "";
			int k;
			for ( k = 0; k < 8; k++ ) {
				Q_strcat( buf, sizeof( buf ), va( "%s%i,%i,%i,%i", k ? " " : "", nbStuckAt[k][0], nbStuckAt[k][1], nbStuckAt[k][2], nbStuckAt[k][3] ) );
			}
			BG_OAXDebugSet( "g_navbot_stuck_at", buf );
		}
		BG_OAXDebugSetInt( "g_nav_stuck", nbStuck );
		BG_OAXDebugSetInt( "g_navbot_links_taken", nbLinksTaken );
		BG_OAXDebugSetInt( "g_navbot_links_done", nbLinksDone );
		BG_OAXDebugSetInt( "g_navbot_links_failed", nbLinksFailed );
		BG_OAXDebugSetInt( "g_navbot_link_kinds", nbLinkKindsDone );
		{
			static const int kinds[6] = { OAX_NAV_TELEPORT, OAX_NAV_JUMPPAD, OAX_NAV_LADDER, OAX_NAV_JUMP, OAX_NAV_DROP, OAX_NAV_TRANSLOCATOR };
			static const char *names[6] = { "teleport", "jumppad", "ladder", "jump", "drop", "translocator" };
			int k;
			for ( k = 0; k < 6; k++ ) {
				int *st = nbKindStats[NB_KindSlot( kinds[k] )];
				BG_OAXDebugSet( va( "g_navbot_lk_%s", names[k] ), va( "%i %i %i", st[0], st[1], st[2] ) );
				if ( nbLinkFail[NB_KindSlot( kinds[k] )][0] ) {
					BG_OAXDebugSet( va( "g_navbot_lf_%s", names[k] ), nbLinkFail[NB_KindSlot( kinds[k] )] );
				}
			}
		}
		BG_OAXDebugSet( "g_navbot_hash", va( "%08x", nbHash ) );
	}
}
