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
g_oax_assault.c: Assault (g_gametype GT_ASSAULT).

One team attacks a base against the clock, the other defends it. The
attackers complete the map's objectives (func_oax_objective) in order;
completing the final one wins the round. Then the map restarts with the
roles swapped: the new attackers must finish faster than the first ones
did (or at all, if the first ones failed). A match is the pair of rounds:

  round 1 made it, round 2 beat its time    -> round 2's attackers win
  round 1 made it, round 2 did not          -> round 1's attackers win
  round 1 failed, round 2 made it           -> round 2's attackers win
  both failed                               -> a draw

The winner gets one team point; then the intermission.

Map entities:

- func_oax_objective (a brush entity, or a point one for reach and
  trigger): "type" destroy | reach | use | trigger, "id" a short name
  spawns refer to, "name" what the HUD calls it ("the gate generator"),
  "order" (objectives open when every one of a lower order is done; equal
  orders can go in any order), "final" 1 (completing it wins the round),
  "health" (destroy, default 600), "radius" (use: how close, default 96;
  a point reach objective: how close, default 128), "usetime" (use:
  seconds of holding use, default 4), "message" (announced when it is
  done), "target" / "call" (fired when it is done: doors, scripts). A
  destroy objective is solid and shootable by the attackers only; it
  disappears in explosions. A brush reach objective is an invisible
  trigger volume. A use objective is whatever the brush is (a console);
  attackers stand beside it holding use (the use button is theirs there,
  not a holdable item's or a vehicle's).
- info_oax_assault_spawn (a point): "role" attack | defend, "after" an
  objective id (active once that one is done), "until" an id (active
  until that one is done). Players spawn at an active spot of their role.
- info_oax_assault (a point, optional): "time" the round's time limit in
  seconds (default 480; g_oaxAssaultTime overrides), "message" the
  attackers' briefing.

The state that crosses the map_restart between the rounds is the cvar
g_oaxAssaultState ("round firstAttackers round1Time"); a new map starts
round 1. g_oaxAssaultFirst 1 lets blue attack first (default red).

Debug values: g_as_* (round, phase, attackers, objectives done, each
objective's state, the outcome). Server command (tests): assault complete
<id>, assault limit <seconds>.
===========================================================================
*/
#include "g_local.h"
#include "oax_public.h"
#include "bg_oax_assault.h"

#define AS_PRE_MS		3000	/* the clock starts this long after the round's start */
#define AS_OVER_MS		7000	/* the result shows this long before the next round or the end */
#define AS_USE_GRACE	200		/* use counts as held this long after the last command with it */
#define AS_DEFAULT_TIME	480

typedef struct {
	gentity_t	*ent;
	char		id[32];
	char		name[64];
	char		message[128];
	int			type;
	int			order;
	int			final;
	int			maxHealth;
	float		radius;
	int			useMs;
	float		progress;		/* use: 0..1 */
	int			lastUse;		/* level time someone last held use beside it */
	int			user;
	int			state;
	int			doneTime;
	vec3_t		point;
	char		sent[MAX_INFO_STRING];
} asObjective_t;

static asObjective_t	asObj[OAX_AS_MAX_OBJECTIVES];
#define AS_MAX_DEFEND	64
static gentity_t		*asDefend[AS_MAX_DEFEND];	/* info_oax_assault_defend posts */
static int				asNumDefend;
static int				asNumObj;
static int				asTimeKey;		/* info_oax_assault "time", 0 none */
static char				asBriefing[128];

static struct {
	int		active;
	int		round;
	int		first;			/* round 1's attackers */
	int		attackers;
	int		r1Time;			/* round 1: ms it took, -1 failed, 0 not over */
	int		phase;
	int		start;			/* level time the clock starts */
	int		limit;			/* ms */
	int		end;			/* level time the round ended */
	int		outcome;		/* 1 made it, 2 ran out of time */
	int		decided;
	char	sent[MAX_INFO_STRING];
} as;

static vmCvar_t	g_oaxAssaultTime;
static vmCvar_t	g_oaxAssaultFirst;
static vmCvar_t	g_oaxAssaultState;

qboolean G_OAXAssaultActive( void ) {
	return as.active;
}

int G_OAXAssaultAttackers( void ) {
	return as.active ? as.attackers : TEAM_FREE;
}

/* ---- the objectives ---------------------------------------------------------------- */

static asObjective_t *AS_OfEnt( gentity_t *ent ) {
	int i;

	for ( i = 0; i < asNumObj; i++ ) {
		if ( asObj[i].ent == ent ) {
			return &asObj[i];
		}
	}
	return NULL;
}

static asObjective_t *AS_OfId( const char *id ) {
	int i;

	for ( i = 0; i < asNumObj; i++ ) {
		if ( !Q_stricmp( asObj[i].id, id ) ) {
			return &asObj[i];
		}
	}
	return NULL;
}

/* open: every objective of a lower order is done */
static qboolean AS_Open( const asObjective_t *o ) {
	int i;

	if ( o->state == OAX_ASOS_DONE ) {
		return qfalse;
	}
	for ( i = 0; i < asNumObj; i++ ) {
		if ( asObj[i].order < o->order && asObj[i].state != OAX_ASOS_DONE ) {
			return qfalse;
		}
	}
	return qtrue;
}

static void AS_UpdateStates( void ) {
	int i;

	for ( i = 0; i < asNumObj; i++ ) {
		if ( asObj[i].state != OAX_ASOS_DONE ) {
			asObj[i].state = AS_Open( &asObj[i] ) ? OAX_ASOS_ACTIVE : OAX_ASOS_LOCKED;
		}
	}
}

static qboolean AS_IsAttacker( gentity_t *ent ) {
	return ent && ent->client && ent->health > 0 && ent->client->sess.sessionTeam == as.attackers;
}

static void AS_PrintAll( const char *msg ) {
	trap_SendServerCommand( -1, va( "cp \"%s\"", msg ) );
	trap_SendServerCommand( -1, va( "print \"%s\n\"", msg ) );
}

/* m:ss into buf (not va: the messages put several clocks in one va) */
static char *AS_Clock( int ms, char *buf, int size ) {
	int s = ( ms + 999 ) / 1000;
	Com_sprintf( buf, size, "%i:%02i", s / 60, s % 60 );
	return buf;
}

static void AS_EndRound( int outcome );

/* the attackers completed an objective */
static void AS_Complete( asObjective_t *o, gentity_t *activator ) {
	gentity_t *te;

	if ( as.phase != OAX_AS_LIVE || o->state != OAX_ASOS_ACTIVE ) {
		return;
	}
	o->state = OAX_ASOS_DONE;
	o->doneTime = level.time;
	if ( o->type == OAX_ASO_DESTROY && o->ent ) {
		/* gone in a few explosions */
		vec3_t p, up = { 0, 0, 1 };
		int k;
		for ( k = 0; k < 3; k++ ) {
			VectorCopy( o->point, p );
			p[0] += ( k - 1 ) * 24;
			p[2] += ( k - 1 ) * 16;
			te = G_TempEntity( p, EV_MISSILE_MISS );
			te->s.weapon = WP_ROCKET_LAUNCHER;
			te->s.eventParm = DirToByte( up );
		}
		o->ent->takedamage = qfalse;
		o->ent->r.contents = 0;
		o->ent->r.svFlags |= SVF_NOCLIENT;
		trap_LinkEntity( o->ent );
	}
	if ( activator && activator->client ) {
		AddScore( activator, o->point, 10 );
	}
	AS_PrintAll( o->message[0] ? o->message : va( "%s: done", o->name ) );
	te = G_TempEntity( o->point, EV_GLOBAL_TEAM_SOUND );
	te->s.eventParm = as.attackers == TEAM_RED ? GTS_REDTEAM_SCORED : GTS_BLUETEAM_SCORED;
	te->r.svFlags |= SVF_BROADCAST;
	if ( o->ent ) {
		G_UseTargets( o->ent, activator ? activator : o->ent );
	}
	AS_UpdateStates();
	if ( o->final ) {
		AS_EndRound( 1 );
	}
}

/* G_Damage: qtrue when targ is an objective (it took or ignored the hit) */
qboolean G_OAXAssaultDamage( gentity_t *targ, gentity_t *attacker, int damage ) {
	asObjective_t *o;

	if ( !asNumObj || !( o = AS_OfEnt( targ ) ) ) {
		return qfalse;
	}
	if ( o->type != OAX_ASO_DESTROY || as.phase != OAX_AS_LIVE || o->state != OAX_ASOS_ACTIVE ||
		!attacker || !attacker->client || attacker->client->sess.sessionTeam != as.attackers ) {
		return qtrue;
	}
	targ->health -= damage;
	if ( targ->health <= 0 ) {
		targ->health = 0;
		AS_Complete( o, attacker );
	}
	return qtrue;
}

static void AS_Touch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	asObjective_t *o = AS_OfEnt( self );

	if ( o && o->type == OAX_ASO_REACH && AS_IsAttacker( other ) ) {
		AS_Complete( o, other );
	}
}

static void AS_Use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	asObjective_t *o = AS_OfEnt( self );

	if ( o && o->type == OAX_ASO_TRIGGER ) {
		AS_Complete( o, activator );
	}
}

/*QUAKED func_oax_objective (1 .5 0) ?
An Assault objective (g_gametype GT_ASSAULT): see g_oax_assault.c.
"type" destroy, reach, use or trigger; "id"; "name"; "order"; "final";
"health"; "radius"; "usetime"; "message"; "target"; "call".
*/
void SP_func_oax_objective( gentity_t *ent ) {
	asObjective_t *o;
	char *s;
	float f;

	if ( g_gametype.integer != GT_ASSAULT || asNumObj >= OAX_AS_MAX_OBJECTIVES ) {
		if ( asNumObj >= OAX_AS_MAX_OBJECTIVES ) {
			G_Printf( S_COLOR_YELLOW "func_oax_objective: more than %i objectives\n", OAX_AS_MAX_OBJECTIVES );
		}
		G_FreeEntity( ent );
		return;
	}
	o = &asObj[asNumObj++];
	memset( o, 0, sizeof( *o ) );
	o->ent = ent;
	ent->classname = "func_oax_objective";
	G_SpawnString( "type", "destroy", &s );
	o->type = !Q_stricmp( s, "reach" ) ? OAX_ASO_REACH : !Q_stricmp( s, "use" ) ? OAX_ASO_USE :
		!Q_stricmp( s, "trigger" ) ? OAX_ASO_TRIGGER : OAX_ASO_DESTROY;
	G_SpawnString( "id", va( "obj%i", asNumObj ), &s );
	Q_strncpyz( o->id, s, sizeof( o->id ) );
	G_SpawnString( "name", o->id, &s );
	Q_strncpyz( o->name, s, sizeof( o->name ) );
	G_SpawnString( "message", "", &s );
	Q_strncpyz( o->message, s, sizeof( o->message ) );
	G_SpawnInt( "order", "1", &o->order );
	G_SpawnInt( "final", "0", &o->final );
	G_SpawnInt( "health", "600", &o->maxHealth );
	G_SpawnFloat( "radius", o->type == OAX_ASO_USE ? "96" : "128", &o->radius );
	G_SpawnFloat( "usetime", "4", &f );
	o->useMs = (int)( f * 1000.0f );
	if ( o->useMs < 100 ) {
		o->useMs = 100;
	}
	for ( s = o->name; *s; s++ ) {
		if ( *s == '\\' || *s == '"' || *s == ';' ) {
			*s = ' ';
		}
	}

	if ( ent->model && ent->model[0] == '*' ) {
		trap_SetBrushModel( ent, ent->model );
		if ( o->type == OAX_ASO_REACH ) {
			ent->r.contents = CONTENTS_TRIGGER;
			ent->r.svFlags |= SVF_NOCLIENT;
			ent->touch = AS_Touch;
		} else {
			/* a solid, drawn brush model (as func_static) */
			ent->s.eType = ET_MOVER;
			ent->r.contents = CONTENTS_SOLID;
			VectorCopy( ent->s.origin, ent->s.pos.trBase );
			VectorCopy( ent->s.origin, ent->r.currentOrigin );
			ent->s.pos.trType = TR_STATIONARY;
		}
		trap_LinkEntity( ent );
		VectorAdd( ent->r.absmin, ent->r.absmax, o->point );
		VectorScale( o->point, 0.5f, o->point );
	} else {
		VectorCopy( ent->s.origin, o->point );
		ent->r.svFlags |= SVF_NOCLIENT;
	}
	if ( o->type == OAX_ASO_DESTROY ) {
		ent->takedamage = qtrue;
		ent->health = o->maxHealth;
	}
	ent->use = AS_Use;
}

/*QUAKED info_oax_assault_spawn (1 0 .5) (-16 -16 -24) (16 16 32)
An Assault spawn point: "role" attack or defend, "after" an objective id
(active once it is done), "until" an id (active until it is done), "angle".
*/
void SP_info_oax_assault_spawn( gentity_t *ent ) {
	char *s;

	if ( g_gametype.integer != GT_ASSAULT ) {
		G_FreeEntity( ent );
		return;
	}
	G_SpawnString( "role", "attack", &s );
	ent->count = !Q_stricmp( s, "defend" ) ? 1 : 0;
	G_SpawnString( "after", "", &s );
	ent->message = G_NewString( s );
	G_SpawnString( "until", "", &s );
	ent->team = G_NewString( s );
	ent->r.svFlags |= SVF_NOCLIENT;
}

/*QUAKED info_oax_assault (1 0 .5) (-8 -8 -8) (8 8 8)
The map's Assault settings: "time" the round's limit in seconds (480),
"message" the attackers' briefing.
*/
void SP_info_oax_assault( gentity_t *ent ) {
	char *s;

	G_SpawnInt( "time", "0", &asTimeKey );
	G_SpawnString( "message", "", &s );
	Q_strncpyz( asBriefing, s, sizeof( asBriefing ) );
	for ( s = asBriefing; *s; s++ ) {
		if ( *s == '\\' || *s == '"' || *s == ';' ) {
			*s = ' ';
		}
	}
	ent->r.svFlags |= SVF_NOCLIENT;
}

/* ---- gates: role / after / until on triggers and Assault entities --------------------- */

typedef struct {
	gentity_t	*ent;		/* the entity the keys were read for (slots are reused) */
	int			role;		/* -1 any, 0 attack, 1 defend */
	char		after[32];
	char		until[32];
	int			navBit;		/* its navmesh link's exclusion bit, 0 none */
} asGate_t;

static asGate_t	asGates[MAX_GENTITIES];
static int		asGateBitsUsed;
/* link flag bits the engine's navmesh leaves free (g_oax_nav.h OAX_NAV_*) */
static const int asGateBits[] = { 0x0004, 0x0008, 0x0200, 0x0400, 0x0800, 0x2000, 0x4000, 0x8000 };

/* after the entity's spawn function: its role / after / until keys */
void G_OAXAssaultGateSpawn( gentity_t *ent ) {
	asGate_t *g;
	char *role, *after, *until;

	if ( !ent || !ent->inuse || ent->s.number >= MAX_GENTITIES || g_gametype.integer != GT_ASSAULT ||
		!Q_stricmp( ent->classname, "info_oax_assault_spawn" ) ) {
		return;
	}
	G_SpawnString( "role", "", &role );
	G_SpawnString( "after", "", &after );
	G_SpawnString( "until", "", &until );
	if ( !role[0] && !after[0] && !until[0] ) {
		return;
	}
	g = &asGates[ent->s.number];
	memset( g, 0, sizeof( *g ) );
	g->ent = ent;
	g->role = !Q_stricmp( role, "attack" ) ? 0 : !Q_stricmp( role, "defend" ) ? 1 : -1;
	Q_strncpyz( g->after, after, sizeof( g->after ) );
	Q_strncpyz( g->until, until, sizeof( g->until ) );
}

static asGate_t *AS_Gate( gentity_t *ent ) {
	asGate_t *g;

	if ( !ent || ent->s.number >= MAX_GENTITIES ) {
		return NULL;
	}
	g = &asGates[ent->s.number];
	return g->ent == ent && ent->inuse ? g : NULL;
}

/* the stage part of a gate: after done, until not done */
static qboolean AS_StageOpen( const char *after, const char *until ) {
	asObjective_t *o;

	if ( after[0] && ( !( o = AS_OfId( after ) ) || o->state != OAX_ASOS_DONE ) ) {
		return qfalse;
	}
	if ( until[0] && ( o = AS_OfId( until ) ) && o->state == OAX_ASOS_DONE ) {
		return qfalse;
	}
	return qtrue;
}

static qboolean AS_GateOpenFor( asGate_t *g, int team ) {
	if ( g->role >= 0 && ( team == as.attackers ? 0 : 1 ) != g->role ) {
		return qfalse;
	}
	return AS_StageOpen( g->after, g->until );
}

/* may this player use this trigger now (always, outside Assault or without keys) */
qboolean G_OAXAssaultGateOpen( gentity_t *trigger, gentity_t *player ) {
	asGate_t *g;

	if ( !as.active || !( g = AS_Gate( trigger ) ) || !player || !player->client ) {
		return qtrue;
	}
	return AS_GateOpenFor( g, player->client->sess.sessionTeam );
}

/* is an Assault entity's stage open (after / until), whoever asks */
qboolean G_OAXAssaultStageOpen( gentity_t *ent ) {
	asGate_t *g;

	if ( !as.active || !( g = AS_Gate( ent ) ) ) {
		return qtrue;
	}
	return AS_StageOpen( g->after, g->until );
}

/* the role key of an Assault entity: -1 any, 0 attack, 1 defend */
int G_OAXAssaultRole( gentity_t *ent ) {
	asGate_t *g = AS_Gate( ent );
	return g ? g->role : -1;
}

/* the navmesh link of a gated teleporter gets a bit of its own (up to 8) */
int G_OAXAssaultGateNavBit( int entnum ) {
	asGate_t *g;

	if ( entnum < 0 || entnum >= MAX_GENTITIES || !( g = AS_Gate( &g_entities[entnum] ) ) ) {
		return 0;
	}
	if ( !g->navBit ) {
		if ( asGateBitsUsed >= (int)( sizeof( asGateBits ) / sizeof( asGateBits[0] ) ) ) {
			G_Printf( S_COLOR_YELLOW "assault: more than %i gated teleporters; bots may path through closed ones\n", asGateBitsUsed );
			return 0;
		}
		g->navBit = asGateBits[asGateBitsUsed++];
	}
	return g->navBit;
}

/* the link bits of the teleporters closed to this team now */
int G_OAXAssaultNavExclude( int team ) {
	int i, bits = 0;

	if ( !as.active || !asGateBitsUsed ) {
		return 0;
	}
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		asGate_t *g = &asGates[i];
		if ( g->navBit && g->ent && g->ent->inuse && !AS_GateOpenFor( g, team ) ) {
			bits |= g->navBit;
		}
	}
	return bits;
}

/* ---- spawning ---------------------------------------------------------------------- */

static qboolean AS_SpawnActive( gentity_t *spot ) {
	asObjective_t *o;

	if ( spot->message && spot->message[0] && ( !( o = AS_OfId( spot->message ) ) || o->state != OAX_ASOS_DONE ) ) {
		return qfalse;
	}
	if ( spot->team && spot->team[0] && ( o = AS_OfId( spot->team ) ) && o->state == OAX_ASOS_DONE ) {
		return qfalse;
	}
	return qtrue;
}

/* a spawn spot for a player of this team: an active one of its role, not
   telefragging when there is a choice; NULL when the map has none */
gentity_t *G_OAXAssaultSpawnPoint( int team, vec3_t origin, vec3_t angles ) {
	gentity_t *spots[64], *spot = NULL;
	int n = 0, clear = 0, role, pass;

	if ( !as.active ) {
		return NULL;
	}
	role = team == as.attackers ? 0 : 1;
	/* first the spots nobody stands on, then any */
	for ( pass = 0; pass < 2 && !n; pass++ ) {
		spot = NULL;
		while ( ( spot = G_Find( spot, FOFS( classname ), "info_oax_assault_spawn" ) ) != NULL && n < 64 ) {
			if ( spot->count != role || !AS_SpawnActive( spot ) ) {
				continue;
			}
			if ( pass == 0 && SpotWouldTelefrag( spot ) ) {
				continue;
			}
			spots[n++] = spot;
		}
		clear = pass == 0;
	}
	(void)clear;
	if ( !n ) {
		return NULL;
	}
	spot = spots[rand() % n];
	VectorCopy( spot->s.origin, origin );
	origin[2] += 9;
	VectorCopy( spot->s.angles, angles );
	return spot;
}

/* ---- using --------------------------------------------------------------------------- */

/* the distance from a point to an objective (its box, or its point) */
static float AS_Dist( const asObjective_t *o, const vec3_t p ) {
	vec3_t d;
	int i;

	if ( o->ent && o->ent->model && o->ent->model[0] == '*' ) {
		for ( i = 0; i < 3; i++ ) {
			d[i] = p[i] < o->ent->r.absmin[i] ? o->ent->r.absmin[i] - p[i] : p[i] > o->ent->r.absmax[i] ? p[i] - o->ent->r.absmax[i] : 0;
		}
		return VectorLength( d );
	}
	return Distance( o->point, p );
}

/* ClientThink, before the vehicles and pmove: an attacker holding use
   beside an open use objective works it (and the button stops there) */
void G_OAXAssaultClientThink( gentity_t *ent, usercmd_t *ucmd ) {
	int i;

	if ( !as.active || as.phase != OAX_AS_LIVE || !( ucmd->buttons & BUTTON_USE_HOLDABLE ) || !AS_IsAttacker( ent ) ) {
		return;
	}
	for ( i = 0; i < asNumObj; i++ ) {
		asObjective_t *o = &asObj[i];
		if ( o->type == OAX_ASO_USE && o->state == OAX_ASOS_ACTIVE && AS_Dist( o, ent->client->ps.origin ) <= o->radius ) {
			o->lastUse = level.time;
			o->user = ent->s.number;
			ucmd->buttons &= ~BUTTON_USE_HOLDABLE;
			return;
		}
	}
}

/* ---- the round ----------------------------------------------------------------------- */

static void AS_SaveState( int round, int r1Time ) {
	trap_Cvar_Set( "g_oaxAssaultState", va( "%i %i %i", round, as.first, r1Time ) );
}

static void AS_EndRound( int outcome ) {
	int took = level.time - as.start, winner = TEAM_FREE;
	char msg[256], c1[16], c2[16];
	const char *attackers = TeamName( as.attackers ), *defenders = TeamName( OtherTeam( as.attackers ) );

	if ( as.phase == OAX_AS_OVER ) {
		return;
	}
	as.phase = OAX_AS_OVER;
	as.end = level.time;
	as.outcome = outcome;
	if ( as.round == 1 ) {
		as.r1Time = outcome == 1 ? ( took > 0 ? took : 1 ) : -1;
		AS_Clock( took, c1, sizeof( c1 ) );
		if ( outcome == 1 ) {
			Com_sprintf( msg, sizeof( msg ), "%s took the base in %s\nNext: %s attacks, to beat %s", attackers, c1, defenders, c1 );
		} else {
			Com_sprintf( msg, sizeof( msg ), "%s held the base\nNext: %s attacks", defenders, defenders );
		}
		AS_PrintAll( msg );
		return;
	}
	/* round 2 decides */
	AS_Clock( took, c1, sizeof( c1 ) );
	AS_Clock( as.r1Time, c2, sizeof( c2 ) );
	if ( outcome == 1 ) {
		winner = as.attackers;
		if ( as.r1Time > 0 ) {
			Com_sprintf( msg, sizeof( msg ), "%s took the base in %s, faster than %s\n%s wins", attackers, c1, c2, attackers );
		} else {
			Com_sprintf( msg, sizeof( msg ), "%s took the base\n%s wins", attackers, attackers );
		}
	} else if ( as.r1Time > 0 ) {
		winner = as.first;
		Com_sprintf( msg, sizeof( msg ), "%s held the base\n%s wins", defenders, TeamName( winner ) );
	} else {
		Q_strncpyz( msg, "Both bases held\nA draw", sizeof( msg ) );
	}
	as.decided = winner;
	AS_PrintAll( msg );
}

static void AS_Publish( void ) {
	char buf[MAX_INFO_STRING];
	int i, done = 0;

	Com_sprintf( buf, sizeof( buf ), "\\r\\%i\\a\\%i\\p\\%i\\s\\%i\\l\\%i\\t\\%i\\e\\%i\\w\\%i\\m\\%s",
		as.round, as.attackers, as.phase, as.start, as.limit, as.r1Time, as.phase == OAX_AS_OVER ? as.end : 0, as.outcome, asBriefing );
	if ( strcmp( buf, as.sent ) ) {
		Q_strncpyz( as.sent, buf, sizeof( as.sent ) );
		trap_SetConfigstring( CS_OAX_ASSAULT, buf );
	}
	for ( i = 0; i < asNumObj; i++ ) {
		asObjective_t *o = &asObj[i];
		int health = o->type == OAX_ASO_DESTROY ? ( o->ent && o->maxHealth > 0 ? o->ent->health * 100 / o->maxHealth : 0 ) : 100;
		if ( o->state == OAX_ASOS_DONE ) {
			health = 0;
			done++;
		}
		Com_sprintf( buf, sizeof( buf ), "\\t\\%i\\o\\%i\\s\\%i\\h\\%i\\p\\%i\\f\\%i\\x\\%i\\y\\%i\\z\\%i\\n\\%s",
			o->type, o->order, o->state, health, (int)( o->progress * 100.0f ), o->final,
			(int)o->point[0], (int)o->point[1], (int)o->point[2], o->name );
		if ( strcmp( buf, o->sent ) ) {
			Q_strncpyz( o->sent, buf, sizeof( o->sent ) );
			trap_SetConfigstring( CS_OAX_ASSAULTOBJ + i, buf );
		}
		BG_OAXDebugSet( va( "g_as_obj%i", i ), va( "%s %i %i %i %i", o->id, o->type, o->state, health, (int)( o->progress * 100.0f ) ) );
	}
	if ( g_entities[0].inuse && g_entities[0].client ) {
		gclient_t *cl = g_entities[0].client;
		BG_OAXDebugSet( "g_as_p0", va( "%i %.0f %.0f %.0f %i", cl->sess.sessionTeam, cl->ps.origin[0], cl->ps.origin[1], cl->ps.origin[2],
			cl->ps.stats[STAT_HEALTH] ) );
	}
	BG_OAXDebugSetInt( "g_as_round", as.round );
	BG_OAXDebugSetInt( "g_as_phase", as.phase );
	BG_OAXDebugSetInt( "g_as_attackers", as.attackers );
	BG_OAXDebugSetInt( "g_as_objectives", asNumObj );
	BG_OAXDebugSetInt( "g_as_done", done );
	BG_OAXDebugSetInt( "g_as_limit", as.limit );
	BG_OAXDebugSetInt( "g_as_left", as.phase == OAX_AS_LIVE ? as.limit - ( level.time - as.start ) : 0 );
	BG_OAXDebugSetInt( "g_as_r1time", as.r1Time );
	BG_OAXDebugSetInt( "g_as_outcome", as.outcome );
	BG_OAXDebugSetInt( "g_as_winner", as.decided );
}

void G_OAXAssaultInit( int restart ) {
	trap_Cvar_Register( &g_oaxAssaultTime, "g_oaxAssaultTime", "0", CVAR_ARCHIVE );
	trap_Cvar_Register( &g_oaxAssaultFirst, "g_oaxAssaultFirst", "0", CVAR_ARCHIVE );
	trap_Cvar_Register( &g_oaxAssaultState, "g_oaxAssaultState", "", CVAR_TEMP );
	memset( &as, 0, sizeof( as ) );
	if ( g_gametype.integer != GT_ASSAULT ) {
		return;
	}
	as.active = 1;
	as.round = 1;
	as.first = g_oaxAssaultFirst.integer ? TEAM_BLUE : TEAM_RED;
	if ( restart && g_oaxAssaultState.string[0] ) {
		int r = 1, f = as.first, t = 0;
		const char *p = g_oaxAssaultState.string;
		r = atoi( COM_Parse( (char **)&p ) );
		f = atoi( COM_Parse( (char **)&p ) );
		t = atoi( COM_Parse( (char **)&p ) );
		if ( r == 2 && ( f == TEAM_RED || f == TEAM_BLUE ) ) {
			as.round = 2;
			as.first = f;
			as.r1Time = t;
		}
	}
	as.attackers = as.round == 1 ? as.first : OtherTeam( as.first );
	as.limit = ( g_oaxAssaultTime.integer > 0 ? g_oaxAssaultTime.integer : asTimeKey > 0 ? asTimeKey : AS_DEFAULT_TIME ) * 1000;
	if ( as.round == 2 && as.r1Time > 0 ) {
		as.limit = as.r1Time;
	}
	as.start = level.time + AS_PRE_MS;
	as.phase = OAX_AS_PRE;
	/* a fresh round 1 forgets any earlier match on this map */
	AS_SaveState( as.round, as.round == 2 ? as.r1Time : 0 );
	AS_UpdateStates();
	AS_Publish();
	G_Printf( "assault: round %i, %s attacks, %i objectives, %i s\n", as.round, TeamName( as.attackers ), asNumObj, as.limit / 1000 );
}

void G_OAXAssaultFrame( void ) {
	int i, ms;
	char clock[16];

	if ( !as.active ) {
		return;
	}
	ms = level.time - level.previousTime;
	if ( as.phase == OAX_AS_PRE && level.time >= as.start ) {
		as.phase = OAX_AS_LIVE;
		for ( i = 0; i < level.maxclients; i++ ) {
			gclient_t *cl = &level.clients[i];
			if ( cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
				continue;
			}
			trap_SendServerCommand( i, va( "cp \"%s\n%s\"", cl->sess.sessionTeam == as.attackers ? "ATTACK" : "DEFEND",
				cl->sess.sessionTeam == as.attackers ? ( asBriefing[0] ? asBriefing : "Complete the objectives" ) :
				va( "Hold out for %s", AS_Clock( as.limit, clock, sizeof( clock ) ) ) ) );
		}
	}
	if ( as.phase == OAX_AS_LIVE ) {
		for ( i = 0; i < asNumObj; i++ ) {
			asObjective_t *o = &asObj[i];
			if ( o->state != OAX_ASOS_ACTIVE ) {
				continue;
			}
			if ( o->type == OAX_ASO_USE ) {
				if ( level.time - o->lastUse <= AS_USE_GRACE ) {
					o->progress += (float)ms / o->useMs;
				} else {
					/* it slips back, three times slower than it builds */
					o->progress -= (float)ms / ( o->useMs * 3 );
					if ( o->progress < 0 ) {
						o->progress = 0;
					}
				}
				if ( o->progress >= 1.0f ) {
					o->progress = 1.0f;
					AS_Complete( o, &g_entities[o->user] );
				}
			} else if ( o->type == OAX_ASO_REACH && !( o->ent && o->ent->model && o->ent->model[0] == '*' ) ) {
				int k;
				for ( k = 0; k < level.maxclients; k++ ) {
					gentity_t *e = &g_entities[k];
					if ( e->inuse && AS_IsAttacker( e ) && AS_Dist( o, e->client->ps.origin ) <= o->radius ) {
						AS_Complete( o, e );
						break;
					}
				}
			}
		}
		if ( as.phase == OAX_AS_LIVE && level.time - as.start >= as.limit ) {
			AS_EndRound( 2 );
		}
	}
	if ( as.phase == OAX_AS_OVER && level.time - as.end >= AS_OVER_MS && !level.intermissionQueued && !level.intermissiontime ) {
		G_Printf( "assault: round %i over, %s\n", as.round, as.round == 1 ? "restarting for round 2" : "the match is decided" );
		if ( as.round == 1 ) {
			AS_SaveState( 2, as.r1Time );
			as.end = level.time + 100000;	/* once */
			trap_SendConsoleCommand( EXEC_INSERT, "map_restart 0\n" );
		} else {
			if ( as.decided == TEAM_RED || as.decided == TEAM_BLUE ) {
				AddTeamScore( level.intermission_origin, as.decided, 1 );
				CalculateRanks();
			}
			AS_SaveState( 1, 0 );
			LogExit( "Assault over." );
		}
	}
	AS_Publish();
}

void G_OAXAssaultShutdown( void ) {
	memset( asGates, 0, sizeof( asGates ) );
	asNumDefend = 0;
	asGateBitsUsed = 0;
	asNumObj = 0;
	asTimeKey = 0;
	asBriefing[0] = '\0';
	as.active = 0;
}

/*
=================
G_OAXAssault_f

assault complete <id> | limit <seconds> (server console; tests)
=================
*/
void G_OAXAssault_f( void ) {
	char cmd[32], arg[32];
	asObjective_t *o;

	if ( !as.active ) {
		G_Printf( "assault: not an Assault game\n" );
		return;
	}
	trap_Argv( 1, cmd, sizeof( cmd ) );
	trap_Argv( 2, arg, sizeof( arg ) );
	if ( !Q_stricmp( cmd, "complete" ) && ( o = AS_OfId( arg ) ) ) {
		AS_Complete( o, NULL );
	} else if ( !Q_stricmp( cmd, "limit" ) ) {
		as.limit = atoi( arg ) * 1000;
	} else {
		G_Printf( "usage: assault complete <id> | limit <seconds>\n" );
	}
}

/* for the bots: the i-th open objective (its point, type, entity); qfalse past the end */
qboolean G_OAXAssaultObjective( int i, vec3_t point, int *type, gentity_t **ent, float *radius ) {
	int k, n = 0;

	for ( k = 0; k < asNumObj; k++ ) {
		if ( asObj[k].state != OAX_ASOS_ACTIVE ) {
			continue;
		}
		if ( n++ == i ) {
			VectorCopy( asObj[k].point, point );
			*type = asObj[k].type;
			*ent = asObj[k].ent;
			*radius = asObj[k].radius;
			return qtrue;
		}
	}
	return qfalse;
}

/*QUAKED info_oax_assault_defend (0 .5 1) (-16 -16 -24) (16 16 32)
A defending bot's post (a UT DefensePoint): "objective" an id (held while
that objective is not done), "priority" (lower first, default 0),
"after" / "until" ids.
*/
void SP_info_oax_assault_defend( gentity_t *ent ) {
	char *s;

	if ( g_gametype.integer != GT_ASSAULT || asNumDefend >= AS_MAX_DEFEND ) {
		G_FreeEntity( ent );
		return;
	}
	G_SpawnString( "objective", "", &s );
	ent->message = G_NewString( s );
	G_SpawnInt( "priority", "0", &ent->count );
	ent->r.svFlags |= SVF_NOCLIENT;
	asDefend[asNumDefend++] = ent;
}

static qboolean AS_DefendActive( gentity_t *ent ) {
	asObjective_t *o;

	if ( ent->message && ent->message[0] && ( o = AS_OfId( ent->message ) ) && o->state == OAX_ASOS_DONE ) {
		return qfalse;
	}
	return G_OAXAssaultStageOpen( ent );
}

/* for the defending bots: their n-th post, the floor under it. The map's
   info_oax_assault_defend posts when any is active (lower priority first,
   then the order in the map), else the active defender spawn spots */
qboolean G_OAXAssaultGuardSpot( int n, vec3_t point ) {
	gentity_t *spots[64], *spot = NULL;
	int count = 0, i, k;

	for ( i = 0; i < asNumDefend && count < 64; i++ ) {
		if ( asDefend[i]->inuse && AS_DefendActive( asDefend[i] ) ) {
			/* insertion by priority, stable */
			for ( k = count; k > 0 && spots[k - 1]->count > asDefend[i]->count; k-- ) {
				spots[k] = spots[k - 1];
			}
			spots[k] = asDefend[i];
			count++;
		}
	}
	if ( count ) {
		spot = spots[( n < 0 ? 0 : n ) % count];
		VectorCopy( spot->s.origin, point );
		point[2] -= 24;
		return qtrue;
	}

	while ( ( spot = G_Find( spot, FOFS( classname ), "info_oax_assault_spawn" ) ) != NULL && count < 64 ) {
		if ( spot->count == 1 && AS_SpawnActive( spot ) ) {
			spots[count++] = spot;
		}
	}
	if ( !count ) {
		return qfalse;
	}
	spot = spots[( n < 0 ? 0 : n ) % count];
	VectorCopy( spot->s.origin, point );
	point[2] -= 24;
	return qtrue;
}

qboolean G_OAXAssaultLive( void ) {
	return as.active && as.phase == OAX_AS_LIVE;
}
