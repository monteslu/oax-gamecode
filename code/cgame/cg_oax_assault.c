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
cg_oax_assault.c: the Assault HUD (g_gametype GT_ASSAULT; the game side is
g_oax_assault.c, the shared formats bg_oax_assault.h).

- Top centre: the team's role (ATTACK or DEFEND), the round's clock
  counting down, the round, and in round 2 the time to beat.
- Left: the objectives in order: the open ones bright with their health or
  progress, the done ones ticked off, the later ones dim.
- In the world: a marker over every open objective with what to do there
  and how far it is.
- While the local attacker works a use objective: a progress bar.

cg_oaxAssaultHud 0 hides it all. Debug value cg_as_hud: round, role
(0 attack, 1 defend), seconds left, open objectives.
===========================================================================
*/
#include "cg_local.h"
#include "../game/oax_public.h"
#include "../game/bg_oax_assault.h"

typedef struct {
	int		valid;
	int		type, order, state, health, progress, final;
	vec3_t	point;
	char	name[64];
} cgAsObj_t;

static struct {
	int			valid;
	int			round, attackers, phase, start, limit, r1Time, end, outcome;
	char		briefing[128];
	cgAsObj_t	obj[OAX_AS_MAX_OBJECTIVES];
} cas;

static vmCvar_t	cg_oaxAssaultHud;

static void CG_AsParseObj( int i ) {
	const char *s = CG_ConfigString( CS_OAX_ASSAULTOBJ + i );
	cgAsObj_t *o = &cas.obj[i];

	o->valid = s[0] != 0;
	if ( !o->valid ) {
		return;
	}
	o->type = atoi( Info_ValueForKey( s, "t" ) );
	o->order = atoi( Info_ValueForKey( s, "o" ) );
	o->state = atoi( Info_ValueForKey( s, "s" ) );
	o->health = atoi( Info_ValueForKey( s, "h" ) );
	o->progress = atoi( Info_ValueForKey( s, "p" ) );
	o->final = atoi( Info_ValueForKey( s, "f" ) );
	o->point[0] = atoi( Info_ValueForKey( s, "x" ) );
	o->point[1] = atoi( Info_ValueForKey( s, "y" ) );
	o->point[2] = atoi( Info_ValueForKey( s, "z" ) );
	Q_strncpyz( o->name, Info_ValueForKey( s, "n" ), sizeof( o->name ) );
}

static void CG_AsParseState( void ) {
	const char *s = CG_ConfigString( CS_OAX_ASSAULT );

	cas.valid = s[0] != 0;
	cas.round = atoi( Info_ValueForKey( s, "r" ) );
	cas.attackers = atoi( Info_ValueForKey( s, "a" ) );
	cas.phase = atoi( Info_ValueForKey( s, "p" ) );
	cas.start = atoi( Info_ValueForKey( s, "s" ) );
	cas.limit = atoi( Info_ValueForKey( s, "l" ) );
	cas.r1Time = atoi( Info_ValueForKey( s, "t" ) );
	cas.end = atoi( Info_ValueForKey( s, "e" ) );
	cas.outcome = atoi( Info_ValueForKey( s, "w" ) );
	Q_strncpyz( cas.briefing, Info_ValueForKey( s, "m" ), sizeof( cas.briefing ) );
}

void CG_OAXAssaultInit( void ) {
	int i;

	trap_Cvar_Register( &cg_oaxAssaultHud, "cg_oaxAssaultHud", "1", CVAR_ARCHIVE );
	memset( &cas, 0, sizeof( cas ) );
	CG_AsParseState();
	for ( i = 0; i < OAX_AS_MAX_OBJECTIVES; i++ ) {
		CG_AsParseObj( i );
	}
}

void CG_OAXAssaultConfigString( int num ) {
	if ( num == CS_OAX_ASSAULT ) {
		CG_AsParseState();
	} else if ( num >= CS_OAX_ASSAULTOBJ && num < CS_OAX_ASSAULTOBJ + OAX_AS_MAX_OBJECTIVES ) {
		CG_AsParseObj( num - CS_OAX_ASSAULTOBJ );
	}
}

static const char *CG_AsClock( int ms ) {
	int s;

	if ( ms < 0 ) {
		ms = 0;
	}
	s = ( ms + 999 ) / 1000;
	return va( "%i:%02i", s / 60, s % 60 );
}

static const char *CG_AsVerb( const cgAsObj_t *o, qboolean attacking ) {
	static const char *attack[] = { "Destroy", "Reach", "Use", "Complete" };
	static const char *defend[] = { "Protect", "Guard", "Guard", "Guard" };

	if ( o->type < 0 || o->type > 3 ) {
		return "";
	}
	return attacking ? attack[o->type] : defend[o->type];
}

/* a world point to the 640x480 screen; qfalse behind the view */
static qboolean CG_AsToScreen( const vec3_t p, float *x, float *y ) {
	vec3_t d;
	float z;

	VectorSubtract( p, cg.refdef.vieworg, d );
	z = DotProduct( d, cg.refdef.viewaxis[0] );
	if ( z < 8 ) {
		return qfalse;
	}
	*x = 320.0f * ( 1.0f - DotProduct( d, cg.refdef.viewaxis[1] ) / z / tan( DEG2RAD( cg.refdef.fov_x * 0.5f ) ) );
	*y = 240.0f * ( 1.0f - DotProduct( d, cg.refdef.viewaxis[2] ) / z / tan( DEG2RAD( cg.refdef.fov_y * 0.5f ) ) );
	return qtrue;
}

static void CG_AsText( float x, float y, const char *s, const float *color, int cw, int ch ) {
	CG_DrawStringExt( (int)x, (int)y, s, color, qfalse, qtrue, cw, ch, 0 );
}

void CG_OAXAssaultHUD( void ) {
	int myTeam = cg.snap ? cg.snap->ps.persistant[PERS_TEAM] : TEAM_SPECTATOR;
	qboolean attacking = myTeam == cas.attackers, spectator = myTeam != TEAM_RED && myTeam != TEAM_BLUE;
	vec4_t back = { 0, 0, 0, 0.5f }, white = { 1, 1, 1, 1 }, dim = { 0.6f, 0.6f, 0.6f, 0.8f }, done = { 0.4f, 0.9f, 0.4f, 0.9f };
	vec4_t role, bar;
	int i, left, open = 0;
	float y;
	const char *s;

	if ( cgs.gametype != GT_ASSAULT || !cas.valid ) {
		return;
	}
	trap_Cvar_Update( &cg_oaxAssaultHud );
	if ( !cg_oaxAssaultHud.integer ) {
		return;
	}

	/* the clock */
	left = cas.phase == OAX_AS_PRE ? cas.limit : cas.phase == OAX_AS_LIVE ? cas.limit - ( cg.time - cas.start ) :
		cas.limit - ( cas.end - cas.start );
	if ( attacking ) {
		role[0] = 1; role[1] = 0.55f; role[2] = 0.15f; role[3] = 1;
	} else {
		role[0] = 0.35f; role[1] = 0.7f; role[2] = 1; role[3] = 1;
	}
	s = spectator ? va( "%s ATTACKS", cas.attackers == TEAM_RED ? "RED" : "BLUE" ) : attacking ? "ATTACK" : "DEFEND";
	CG_FillRect( 250, 4, 140, 40, back );
	CG_AsText( 320 - CG_DrawStrlen( s ) * 5, 6, s, spectator ? white : role, 10, 14 );
	s = CG_AsClock( left );
	if ( cas.phase == OAX_AS_LIVE && left < 30000 && ( cg.time / 300 ) & 1 ) {
		bar[0] = 1; bar[1] = 0.3f; bar[2] = 0.3f; bar[3] = 1;
	} else {
		Vector4Copy( white, bar );
	}
	CG_AsText( 320 - CG_DrawStrlen( s ) * 6, 21, s, bar, 12, 16 );
	s = cas.round == 2 && cas.r1Time > 0 ? va( "round 2   to beat %s", CG_AsClock( cas.r1Time ) ) : va( "round %i", cas.round );
	CG_AsText( 320 - CG_DrawStrlen( s ) * 3, 38, s, dim, 6, 8 );

	/* the objectives */
	y = 120;
	for ( i = 0; i < OAX_AS_MAX_OBJECTIVES; i++ ) {
		cgAsObj_t *o = &cas.obj[i];
		const float *c;
		if ( !o->valid ) {
			continue;
		}
		if ( o->state == OAX_ASOS_DONE ) {
			s = va( "+ %s", o->name );
			c = done;
		} else if ( o->state == OAX_ASOS_ACTIVE ) {
			open++;
			s = o->type == OAX_ASO_DESTROY ? va( "> %s %s  %i%%", CG_AsVerb( o, attacking || spectator ), o->name, o->health ) :
				o->type == OAX_ASO_USE && o->progress > 0 ? va( "> %s %s  %i%%", CG_AsVerb( o, attacking || spectator ), o->name, o->progress ) :
				va( "> %s %s", CG_AsVerb( o, attacking || spectator ), o->name );
			c = white;
		} else {
			s = va( "  %s", o->name );
			c = dim;
		}
		CG_FillRect( 4, y - 1, CG_DrawStrlen( s ) * 7 + 6, 13, back );
		CG_AsText( 7, y, s, c, 7, 11 );
		y += 14;
	}

	/* markers over the open objectives, and the use bar */
	for ( i = 0; i < OAX_AS_MAX_OBJECTIVES; i++ ) {
		cgAsObj_t *o = &cas.obj[i];
		float x, sy, dist;
		vec3_t p;
		if ( !o->valid || o->state != OAX_ASOS_ACTIVE || cas.phase == OAX_AS_OVER ) {
			continue;
		}
		VectorCopy( o->point, p );
		p[2] += 24;
		dist = Distance( cg.refdef.vieworg, o->point );
		if ( CG_AsToScreen( p, &x, &sy ) && x > 8 && x < 632 && sy > 8 && sy < 472 ) {
			s = va( "%s %im", CG_AsVerb( o, attacking || spectator ), (int)( dist / 32.0f ) );
			CG_FillRect( x - 4, sy - 4, 8, 8, role );
			CG_AsText( x - CG_DrawStrlen( s ) * 3, sy + 6, s, white, 6, 9 );
		}
		if ( attacking && o->type == OAX_ASO_USE && o->progress > 0 && dist < 220 ) {
			bar[0] = 1; bar[1] = 0.8f; bar[2] = 0.2f; bar[3] = 0.9f;
			CG_FillRect( 220, 300, 200, 14, back );
			CG_FillRect( 222, 302, 196 * o->progress / 100.0f, 10, bar );
			CG_DrawRect( 220, 300, 200, 14, 1, white );
			s = va( "%s  %i%%", o->name, o->progress );
			CG_AsText( 320 - CG_DrawStrlen( s ) * 3.5f, 286, s, white, 7, 11 );
		}
	}
	BG_OAXDebugSet( "cg_as_hud", va( "%i %i %i %i", cas.round, spectator ? -1 : attacking ? 0 : 1, left / 1000, open ) );
}
