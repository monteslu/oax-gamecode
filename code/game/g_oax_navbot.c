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

Everything is a function of the game state and a per-bot LCG, never a wall
clock, so a match plays the same on every build.
===========================================================================
*/
#include "g_local.h"
#include "oax_public.h"
#include "g_oax_nav.h"
#include "g_oax_vehicle.h"

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
	NBG_OWN_FLAG		/* our dropped flag: touch it to return it */
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
} navBot_t;

static navBot_t	navBots[MAX_CLIENTS];
static byte		nbVisited[MAX_GENTITIES];				/* item visited in this sweep round */
static byte		nbFailed[MAX_CLIENTS][MAX_GENTITIES];	/* this bot found no way to the item */
static int		nbPaths, nbNoPath, nbGoalsReached, nbStuck;
static unsigned	nbHash = 2166136261U;
static int		nbNextHashTime;
static vmCvar_t	bot_oaxNav;		/* 0: steer straight at goals, no navmesh paths (a test control) */

static float NB_Rand( navBot_t *nb ) {
	nb->rng = nb->rng * 1103515245U + 12345U;
	return (float)( ( nb->rng >> 8 ) & 0xffffff ) / 16777216.0f;
}

void G_OAXNavBotInit( void ) {
	trap_Cvar_Register( &bot_oaxNav, "bot_oaxNav", "1", 0 );
	memset( navBots, 0, sizeof( navBots ) );
	memset( nbVisited, 0, sizeof( nbVisited ) );
	memset( nbFailed, 0, sizeof( nbFailed ) );
	nbPaths = nbNoPath = nbGoalsReached = nbStuck = 0;
	nbHash = 2166136261U;
	nbNextHashTime = 0;
}

int G_OAXNavBotsActive( void ) {
	return !trap_AAS_Initialized() && BG_OAXFeature( "nav" ) && trap_OAX_NavStatus() > 0;
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

static void NB_ChooseGoal( int clientNum, navBot_t *nb, gentity_t *ent ) {
	gclient_t *cl = ent->client;
	vec3_t origin;

	VectorCopy( cl->ps.origin, origin );
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
		nb->numPts = 2;
		nb->cur = 1;
		nb->pathTime = time;
		VectorCopy( nb->goal, nb->pathGoal );
		return;
	}
	nb->numPts = trap_OAX_NavFindPath( feet, nb->goal, nb->pts, NB_MAX_POINTS, &flags );
	nb->cur = 1;
	nb->pathTime = time;
	VectorCopy( nb->goal, nb->pathGoal );
	nbPaths++;
	if ( nb->numPts <= 0 || ( ( flags & OAX_NAV_PATH_PARTIAL ) && nb->goalKind == NBG_ITEM ) ) {
		nbNoPath++;
		NB_FailGoal( clientNum, nb );
	}
}

/* ---- the per-frame brain ----------------------------------------------------------- */

static void NB_Think( int clientNum, int time ) {
	navBot_t *nb = &navBots[clientNum];
	gentity_t *ent = &g_entities[clientNum];
	gclient_t *cl = ent->client;
	vec3_t origin, dir, wanted, fwd, right;
	float msec, maxTurn, len;
	int j, mvx = 0, mvy = 0;
	qboolean fighting = qfalse;

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
	if ( nb->goalKind != NBG_NONE && HorizDist( origin, nb->goal ) < 32.0f && fabs( origin[2] + MINS_Z - nb->goal[2] ) < 64.0f ) {
		nbGoalsReached++;
		if ( nb->goalKind == NBG_ITEM ) {
			nbVisited[nb->goalEnt] = 1;
		}
		nb->goalKind = NBG_NONE;
		NB_ChooseGoal( clientNum, nb, ent );
	}

	/* path */
	if ( nb->goalKind != NBG_NONE &&
		( nb->numPts <= 0 || time - nb->pathTime >= NB_REPATH_MS || Distance( nb->goal, nb->pathGoal ) > 64.0f ) ) {
		NB_Repath( clientNum, nb, origin, time );
	}
	VectorClear( dir );
	if ( nb->goalKind != NBG_NONE && nb->numPts > 0 ) {
		float *p;
		while ( nb->cur < nb->numPts - 1 && HorizDist( origin, &nb->pts[nb->cur * 3] ) < 40.0f ) {
			nb->cur++;
		}
		if ( nb->cur >= nb->numPts ) {
			nb->cur = nb->numPts - 1;
		}
		p = &nb->pts[nb->cur * 3];
		dir[0] = p[0] - origin[0];
		dir[1] = p[1] - origin[1];
	}
	len = sqrt( dir[0] * dir[0] + dir[1] * dir[1] );
	if ( len > 0.001f ) {
		dir[0] /= len;
		dir[1] /= len;
	}

	/* stuck: little progress for a second while having somewhere to go */
	if ( time - nb->lastCheck >= NB_STUCK_MS ) {
		if ( nb->goalKind != NBG_NONE && len > 0.001f && Distance( origin, nb->lastPos ) < 32.0f ) {
			nbStuck++;
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
	if ( nb->enemy >= 0 ) {
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
		VectorCopy( o->client->ps.origin, aim );
		aim[2] += 8;
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
	} else if ( fighting ) {
		/* no goal: circle-strafe the enemy */
		mvy = ( ( time / 1000 ) & 1 ) ? 127 : -127;
	}
	if ( time < nb->unstickUntil ) {
		mvy = nb->strafe * 127;
		if ( nb->unstickUntil - time > 450 ) {
			nb->cmd.upmove = 127;
		}
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
	int i, n = 0;
	char cmdBuf[1024];

	trap_Cvar_Update( &bot_oaxNav );

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
		/* oax vehicles: boarding and driving toward the same goal (g_oax_vehbot.c) */
		G_OAXVehBotCommand( i, &navBots[i].cmd, navBots[i].goal, navBots[i].goalKind != NBG_NONE, time );
		trap_BotUserCommand( i, &navBots[i].cmd );
		n++;
	}
	NB_Hash( time );
	if ( level.framenum % 10 == 0 ) {
		BG_OAXDebugSetInt( "g_navbots", n );
		BG_OAXDebugSetInt( "g_navbot_paths", nbPaths );
		BG_OAXDebugSetInt( "g_navbot_nopath", nbNoPath );
		BG_OAXDebugSetInt( "g_navbot_goals", nbGoalsReached );
		BG_OAXDebugSetInt( "g_navbot_stuck", nbStuck );
		BG_OAXDebugSetInt( "g_nav_stuck", nbStuck );
		BG_OAXDebugSet( "g_navbot_hash", va( "%08x", nbHash ) );
	}
}
