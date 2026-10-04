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
cg_oax_fx.c: the cgame side of the oax effects. Each part is
used only when the engine advertises its token, and has a cvar:

- Particles ("particles", cg_oaxParticles): weapon impacts spawn particle
  systems (sparks, smoke, explosions; decls in particles/oax_weapons.prt
  shipped with these QVMs), and func_oax_emitter entities draw theirs.
  A spawned system is a few numbers (decl, origin, axis, start time,
  seed): the renderer computes every particle from them, nothing per
  particle lives here. Systems are added to the scene every frame until
  the renderer reports them done.
- Decals ("decals", cg_oaxDecals): CG_ImpactMark's lasting marks become
  projected decals (box projection onto the world and brush models, with
  the renderer's cap and fade) instead of Q3 mark polygons.
- Trails ("trails", cg_oaxTrails): rockets, grenades and plasma, and any
  entity the map gives an `oaxtrail` key (CS_OAX_TRAILS), leave a ribbon
  through their trajectory history: positions at fixed 16 ms steps of
  cg.time, evaluated from the entity's trajectory (re-evaluated when the
  trajectory changes, e.g. a grenade bounce), plus the drawn position.

- Ground effects (a map opts in with the worldspawn key "oax_groundfx" "1";
  cg_oaxGroundFx 0 turns them off): players wading leave spreading rings
  on the water (particles/oax_ground.prt oax/ripple), vehicles throw dust
  from their wheels and thrusters (oax/ground_dust), rings and spray on
  water (oax/splash), and wheels leave tyre tracks (projected decals of
  oaxfx/tread, one per CG_TRACK_SEG units of travel, CG_TRACK_LIFE ms).

Test commands: oaxfx <decl> x y z [dx dy dz] spawns a particle system;
oaxdecal <shader> x y z dx dy dz radius [angle [lifeMs [r g b]]] projects a decal.
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_oax_vehicle.h"

#define MAX_CG_FX           256
#define MAX_EMITTER_DECLS   256
#define MAX_CG_TRAILS       64
#define TRAIL_HIST          48
#define TRAIL_STEP          16      /* ms between history points */
#define MAX_MAP_TRAILS      64

typedef struct {
	int     inUse;
	oaxFx_t fx;
} cgFx_t;

typedef struct {
	int         entity;             /* -1 free */
	int         seenFrame;
	trajectory_t traj;              /* the trajectory the history was last evaluated with */
	int         num;                /* history points, oldest first */
	float       pts[TRAIL_HIST][4]; /* x y z time */
	oaxTrail_t  style;
} cgTrail_t;

typedef struct {
	int         entity;
	oaxTrail_t  style;
} cgMapTrail_t;

static int          haveParticles, haveDecals, haveTrails;
static vmCvar_t     cg_oaxParticles;
static vmCvar_t     cg_oaxDecals;
static vmCvar_t     cg_oaxTrails;
static vmCvar_t     cg_oaxTestTrail;

static cgFx_t       cgFx[MAX_CG_FX];
static int          nextFx, fxCounter;

static char         emitterCS[MAX_STRING_CHARS];
static int          emitterHandle[MAX_EMITTER_DECLS];

static cgTrail_t    trails[MAX_CG_TRAILS];
static short        trailSlot[MAX_GENTITIES];
static int          trailFrame;
static char         mapTrailCS[MAX_STRING_CHARS];
static cgMapTrail_t mapTrails[MAX_MAP_TRAILS];
static int          numMapTrails;

/* weapon effects */
static int          fxSparks, fxSmoke, fxExplosion, fxPlasma, fxRail, fxBulletSparks;
static qhandle_t    trailSmokeShader, trailGlowShader;
static int          decalsMade, fxSpawned;

/* ground effects */
#define CG_TRACK_SLOTS      32
#define CG_TRACK_SEG        56      /* units of travel per tyre track decal */
#define CG_TRACK_LIFE       9000    /* ms */
typedef struct {
	int     entity;                 /* -1 free */
	int     seenTime;
	int     have[OAX_VEH_MAX_WHEELS];
	vec3_t  last[OAX_VEH_MAX_WHEELS];
} cgTrack_t;

static int          haveGroundFx;
static vmCvar_t     cg_oaxGroundFx;
static int          fxDust, fxRipple, fxSplash;
static qhandle_t    treadShader;
static int          rippleTime[MAX_GENTITIES];
static cgTrack_t    tracks[CG_TRACK_SLOTS];
static int          ripplesMade, tracksMade;

static void CG_OAXGroundFxInit( void );
static void CG_OAXPlayerRipples( void );

/*
=================
Helpers
=================
*/
static void AxisFromDir( const vec3_t dir, vec3_t axis[3] ) {
	VectorNormalize2( dir, axis[2] );
	if ( VectorLength( axis[2] ) < 0.5f ) {
		VectorSet( axis[2], 0, 0, 1 );
	}
	MakeNormalVectors( axis[2], axis[0], axis[1] );
}

static int SeedFor( const vec3_t origin ) {
	int s = cg.time * 31 + fxCounter * 977;

	s ^= (int)( origin[0] * 8 ) * 73856093;
	s ^= (int)( origin[1] * 8 ) * 19349663;
	s ^= (int)( origin[2] * 8 ) * 83492791;
	return s;
}

static int RegisterFx( const char *name ) {
	int h = trap_OAX_R_RegisterFx( name );

	if ( !h ) {
		CG_Printf( "^3cgame: no particle decl %s\n", name );
	}
	return h;
}

/*
=================
CG_OAXFxInit
=================
*/
void CG_OAXFxInit( void ) {
	int i;

	haveParticles = BG_OAXFeature( "particles" );
	haveDecals = BG_OAXFeature( "decals" );
	haveTrails = BG_OAXFeature( "trails" );
	trap_Cvar_Register( &cg_oaxParticles, "cg_oaxParticles", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxDecals, "cg_oaxDecals", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxTrails, "cg_oaxTrails", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxTestTrail, "cg_oaxTestTrail", "", CVAR_CHEAT );
	trap_Cvar_Register( &cg_oaxGroundFx, "cg_oaxGroundFx", "1", CVAR_ARCHIVE );

	memset( cgFx, 0, sizeof( cgFx ) );
	nextFx = fxCounter = 0;
	emitterCS[0] = '\0';
	memset( emitterHandle, 0, sizeof( emitterHandle ) );
	for ( i = 0; i < MAX_CG_TRAILS; i++ ) {
		trails[i].entity = -1;
	}
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		trailSlot[i] = -1;
	}
	trailFrame = 0;
	mapTrailCS[0] = '\0';
	numMapTrails = 0;
	decalsMade = fxSpawned = 0;

	fxSparks = fxSmoke = fxExplosion = fxPlasma = fxRail = fxBulletSparks = 0;
	if ( haveParticles ) {
		fxSparks = RegisterFx( "oax/impact_sparks" );
		fxBulletSparks = RegisterFx( "oax/bullet_sparks" );
		fxSmoke = RegisterFx( "oax/impact_smoke" );
		fxExplosion = RegisterFx( "oax/explosion" );
		fxPlasma = RegisterFx( "oax/plasma_impact" );
		fxRail = RegisterFx( "oax/rail_impact" );
	}
	CG_OAXGroundFxInit();
	if ( haveTrails ) {
		trailSmokeShader = trap_R_RegisterShader( "oaxfx/trailSmoke" );
		trailGlowShader = trap_R_RegisterShader( "oaxfx/trailGlow" );
	}
	trap_AddCommand( "oaxfx" );
	trap_AddCommand( "oaxdecal" );
	BG_OAXDebugSetInt( "cg_fx_particles", haveParticles );
	BG_OAXDebugSetInt( "cg_fx_decals", haveDecals );
	BG_OAXDebugSetInt( "cg_fx_trails", haveTrails );
}

/*
=================
CG_OAXSpawnFx

A particle system at origin, its up axis along dir. Returns 0 when
particles are off or the decl is missing.
=================
*/
int CG_OAXSpawnFx( int handle, const vec3_t origin, const vec3_t dir, float scale, const float *rgba ) {
	cgFx_t *f;

	if ( !haveParticles || !cg_oaxParticles.integer || !handle ) {
		return 0;
	}
	f = &cgFx[nextFx];
	nextFx = ( nextFx + 1 ) % MAX_CG_FX;
	memset( f, 0, sizeof( *f ) );
	f->inUse = 1;
	f->fx.handle = handle;
	f->fx.startTime = cg.time;
	f->fx.seed = SeedFor( origin );
	VectorCopy( origin, f->fx.origin );
	AxisFromDir( dir, f->fx.axis );
	f->fx.scale = scale;
	if ( rgba ) {
		f->fx.rgba[0] = rgba[0];
		f->fx.rgba[1] = rgba[1];
		f->fx.rgba[2] = rgba[2];
		f->fx.rgba[3] = rgba[3];
	}
	fxCounter++;
	fxSpawned++;
	return 1;
}

/*
=================
CG_OAXImpactFx

From CG_MissileHitWall: the particle part of a weapon impact.
=================
*/
void CG_OAXImpactFx( int weapon, const vec3_t origin, const vec3_t dir, const float *color ) {
	float tint[4];

	if ( !haveParticles || !cg_oaxParticles.integer ) {
		return;
	}
	switch ( weapon ) {
	case WP_ROCKET_LAUNCHER:
	case WP_GRENADE_LAUNCHER:
	case WP_BFG:
		CG_OAXSpawnFx( fxExplosion, origin, dir, 1.0f, NULL );
		CG_OAXSpawnFx( fxSmoke, origin, dir, 1.6f, NULL );
		break;
	case WP_PLASMAGUN:
		CG_OAXSpawnFx( fxPlasma, origin, dir, 1.0f, NULL );
		break;
	case WP_RAILGUN:
		if ( color ) {
			tint[0] = color[0];
			tint[1] = color[1];
			tint[2] = color[2];
			tint[3] = 1;
			CG_OAXSpawnFx( fxRail, origin, dir, 1.0f, tint );
		} else {
			CG_OAXSpawnFx( fxRail, origin, dir, 1.0f, NULL );
		}
		break;
	case WP_MACHINEGUN:
	case WP_CHAINGUN:
	case WP_SHOTGUN:
		CG_OAXSpawnFx( fxBulletSparks, origin, dir, 1.0f, NULL );
		CG_OAXSpawnFx( fxSmoke, origin, dir, 0.5f, NULL );
		break;
	default:
		CG_OAXSpawnFx( fxSparks, origin, dir, 1.0f, NULL );
		break;
	}
}

/*
=================
CG_OAXDecal

From CG_ImpactMark (lasting marks): a projected decal. qfalse leaves the
mark to the stock code.
=================
*/
static qboolean CG_OAXDecalLife( qhandle_t shader, const vec3_t origin, const vec3_t dir, float orientation,
		float r, float g, float b, float a, qboolean alphaFade, float radius, int lifeMs ) {
	oaxDecal_t d;
	vec3_t up;

	if ( !haveDecals || !cg_oaxDecals.integer || radius <= 0 ) {
		return qfalse;
	}
	memset( &d, 0, sizeof( d ) );
	d.shader = shader;
	VectorCopy( origin, d.origin );
	VectorNormalize2( dir, d.axis[0] );
	PerpendicularVector( up, d.axis[0] );
	RotatePointAroundVector( d.axis[2], d.axis[0], up, orientation );
	CrossProduct( d.axis[0], d.axis[2], d.axis[1] );
	d.halfSize[0] = radius < 8 ? 8 : radius;
	d.halfSize[1] = radius;
	d.halfSize[2] = radius;
	d.rgba[0] = r;
	d.rgba[1] = g;
	d.rgba[2] = b;
	d.rgba[3] = a;
	d.startTime = cg.time;
	d.lifeMs = lifeMs;
	d.fadeMs = lifeMs < 1000 ? lifeMs : 1000;	/* MARK_FADE_TIME */
	d.flags = alphaFade ? OAXDECAL_ALPHAFADE : 0;
	if ( trap_OAX_R_AddDecal( &d ) > 0 ) {
		decalsMade++;
	}
	return qtrue;
}

qboolean CG_OAXDecal( qhandle_t shader, const vec3_t origin, const vec3_t dir, float orientation,
		float r, float g, float b, float a, qboolean alphaFade, float radius ) {
	return CG_OAXDecalLife( shader, origin, dir, orientation, r, g, b, a, alphaFade, radius, 10000 );	/* MARK_TOTAL_TIME */
}

/*
=================
Emitters
=================
*/
static void CG_OAXEmitterDecls( void ) {
	const char *cs = CG_ConfigString( CS_OAX_FXDECLS );
	char buf[MAX_STRING_CHARS];
	char *p, *tok;
	int i = 0;

	if ( !strcmp( cs, emitterCS ) ) {
		return;
	}
	Q_strncpyz( emitterCS, cs, sizeof( emitterCS ) );
	Q_strncpyz( buf, cs, sizeof( buf ) );
	p = buf;
	while ( i < MAX_EMITTER_DECLS ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		emitterHandle[i++] = haveParticles ? RegisterFx( tok ) : 0;
	}
}

/*
=================
CG_OAXEmitter

From CG_AddCEntity for ET_OAX_EMITTER.
=================
*/
void CG_OAXEmitter( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	oaxFx_t fx;

	if ( !haveParticles || !cg_oaxParticles.integer || !s->frame || s->generic1 < 0 || s->generic1 >= MAX_EMITTER_DECLS ) {
		return;
	}
	CG_OAXEmitterDecls();
	memset( &fx, 0, sizeof( fx ) );
	fx.handle = emitterHandle[s->generic1];
	if ( !fx.handle ) {
		return;
	}
	if ( s->time ) {
		fx.startTime = s->time;            /* a one-shot, from when it was used */
	} else {
		fx.flags = OAXFX_SHADERTIME;       /* looping, on the shader clock */
	}
	fx.seed = (int)s->angles2[1];
	fx.scale = s->angles2[0];
	VectorCopy( cent->lerpOrigin, fx.origin );
	AnglesToAxis( cent->lerpAngles, fx.axis );
	fx.rgba[0] = ( s->constantLight & 255 ) / 255.0f;
	fx.rgba[1] = ( ( s->constantLight >> 8 ) & 255 ) / 255.0f;
	fx.rgba[2] = ( ( s->constantLight >> 16 ) & 255 ) / 255.0f;
	fx.rgba[3] = 1;
	trap_OAX_R_AddFx( &fx );
}

/*
=================
Trails
=================
*/
static void SetTrail( oaxTrail_t *t, qhandle_t shader, float w0, float w1, int life,
		float r0, float g0, float b0, float a0, float r1, float g1, float b1, float a1, float texLength ) {
	memset( t, 0, sizeof( *t ) );
	t->shader = shader;
	t->width[0] = w0;
	t->width[1] = w1;
	t->lifeMs = life;
	t->rgba[0][0] = r0; t->rgba[0][1] = g0; t->rgba[0][2] = b0; t->rgba[0][3] = a0;
	t->rgba[1][0] = r1; t->rgba[1][1] = g1; t->rgba[1][2] = b1; t->rgba[1][3] = a1;
	t->texLength = texLength;
}

static void CG_OAXParseMapTrails( void ) {
	const char *cs = CG_ConfigString( CS_OAX_TRAILS );
	char buf[MAX_STRING_CHARS];
	char *p, *rec, *end;

	if ( !strcmp( cs, mapTrailCS ) ) {
		return;
	}
	Q_strncpyz( mapTrailCS, cs, sizeof( mapTrailCS ) );
	Q_strncpyz( buf, cs, sizeof( buf ) );
	numMapTrails = 0;
	for ( rec = buf; *rec && numMapTrails < MAX_MAP_TRAILS; rec = end + 1 ) {
		cgMapTrail_t *mt = &mapTrails[numMapTrails];
		float w, life, c[4];

		end = strchr( rec, ';' );
		if ( !end ) {
			break;
		}
		*end = '\0';
		p = rec;
		mt->entity = atoi( COM_Parse( &p ) );
		{
			char shaderName[MAX_QPATH];

			Q_strncpyz( shaderName, COM_Parse( &p ), sizeof( shaderName ) );
			w = atof( COM_Parse( &p ) );
			life = atof( COM_Parse( &p ) );
			c[0] = atof( COM_Parse( &p ) );
			c[1] = atof( COM_Parse( &p ) );
			c[2] = atof( COM_Parse( &p ) );
			c[3] = atof( COM_Parse( &p ) );
			if ( w <= 0 ) {
				w = 8;
			}
			if ( life <= 0 ) {
				life = 500;
			}
			SetTrail( &mt->style, trap_R_RegisterShader( shaderName ), w, w * 0.25f, (int)life,
				c[0], c[1], c[2], c[3], c[0], c[1], c[2], 0, 0 );
		}
		if ( mt->entity >= 0 && mt->entity < MAX_GENTITIES ) {
			numMapTrails++;
		}
	}
	BG_OAXDebugSetInt( "cg_map_trails", numMapTrails );
}

/* the trail an entity asks for, if any */
static qboolean TrailStyle( const centity_t *cent, oaxTrail_t *t ) {
	const entityState_t *s = &cent->currentState;
	int i;

	if ( s->eType == ET_MISSILE ) {
		switch ( s->weapon ) {
		case WP_ROCKET_LAUNCHER:
			SetTrail( t, trailSmokeShader, 5, 26, 700, 1.0f, 0.85f, 0.6f, 0.9f, 0.45f, 0.45f, 0.45f, 0.0f, 96 );
			return qtrue;
		case WP_GRENADE_LAUNCHER:
			SetTrail( t, trailSmokeShader, 4, 14, 450, 0.85f, 0.85f, 0.85f, 0.7f, 0.5f, 0.5f, 0.5f, 0.0f, 64 );
			return qtrue;
		case WP_PLASMAGUN:
			SetTrail( t, trailGlowShader, 10, 2, 180, 0.6f, 0.7f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0 );
			return qtrue;
		default:
			return qfalse;
		}
	}
	for ( i = 0; i < numMapTrails; i++ ) {
		if ( mapTrails[i].entity == s->number ) {
			*t = mapTrails[i].style;
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean SameTrajectory( const trajectory_t *a, const trajectory_t *b ) {
	return a->trType == b->trType && a->trTime == b->trTime && a->trDuration == b->trDuration
		&& VectorCompare( a->trBase, b->trBase ) && VectorCompare( a->trDelta, b->trDelta );
}

static void TrailUpdate( centity_t *cent, const oaxTrail_t *style ) {
	cgTrail_t *tr;
	int slot = trailSlot[cent->currentState.number];
	int i, t, first, newest;
	float pts[( TRAIL_HIST + 1 ) * 4];
	int n;
	oaxTrail_t out;
	vec3_t center, mins, maxs;

	/* a brush model's origin is its offset from where it was built: trail its middle */
	VectorClear( center );
	if ( cent->currentState.solid == SOLID_BMODEL && cent->currentState.modelindex > 0
		&& cent->currentState.modelindex < MAX_MODELS ) {
		trap_R_ModelBounds( cgs.inlineDrawModel[cent->currentState.modelindex], mins, maxs );
		VectorAdd( mins, maxs, center );
		VectorScale( center, 0.5f, center );
	}

	if ( slot < 0 || trails[slot].entity != cent->currentState.number ) {
		for ( slot = 0; slot < MAX_CG_TRAILS; slot++ ) {
			if ( trails[slot].entity < 0 ) {
				break;
			}
		}
		if ( slot == MAX_CG_TRAILS ) {
			return;
		}
		tr = &trails[slot];
		memset( tr, 0, sizeof( *tr ) );
		tr->entity = cent->currentState.number;
		tr->traj = cent->currentState.pos;
		trailSlot[tr->entity] = slot;
		/* back-fill from the trajectory's start, or as far as the trail reaches */
		first = cg.time - style->lifeMs;
		if ( tr->traj.trTime > first ) {
			first = tr->traj.trTime;
		}
		first = ( first + TRAIL_STEP - 1 ) / TRAIL_STEP * TRAIL_STEP;
		tr->num = 0;
		for ( t = first; t < cg.time && tr->num < TRAIL_HIST; t += TRAIL_STEP ) {
			BG_EvaluateTrajectory( &tr->traj, t, tr->pts[tr->num] );
			tr->pts[tr->num][3] = t;
			tr->num++;
		}
	}
	tr = &trails[slot];
	tr->seenFrame = trailFrame;
	tr->style = *style;

	/* a new trajectory (a bounce): the points from its start re-evaluated */
	if ( !SameTrajectory( &tr->traj, &cent->currentState.pos ) ) {
		tr->traj = cent->currentState.pos;
		for ( i = 0; i < tr->num; i++ ) {
			if ( tr->pts[i][3] >= tr->traj.trTime ) {
				BG_EvaluateTrajectory( &tr->traj, (int)tr->pts[i][3], tr->pts[i] );
			}
		}
	}

	/* new grid points up to cg.time */
	newest = tr->num ? (int)tr->pts[tr->num - 1][3] : ( cg.time - style->lifeMs ) / TRAIL_STEP * TRAIL_STEP;
	for ( t = newest + TRAIL_STEP; t < cg.time; t += TRAIL_STEP ) {
		if ( tr->num == TRAIL_HIST ) {
			memmove( tr->pts[0], tr->pts[1], sizeof( tr->pts[0] ) * ( TRAIL_HIST - 1 ) );
			tr->num--;
		}
		BG_EvaluateTrajectory( &tr->traj, t, tr->pts[tr->num] );
		tr->pts[tr->num][3] = t;
		tr->num++;
	}

	/* newest first: the drawn position, then the history */
	VectorAdd( cent->lerpOrigin, center, pts );
	pts[3] = 0;
	n = 1;
	for ( i = tr->num - 1; i >= 0; i-- ) {
		float age = cg.time - tr->pts[i][3];

		if ( age <= 0 ) {
			continue;
		}
		VectorAdd( tr->pts[i], center, &pts[n * 4] );
		pts[n * 4 + 3] = age;
		n++;
		if ( age >= style->lifeMs ) {
			break;
		}
	}
	if ( n < 2 ) {
		return;
	}
	out = *style;
	out.numPoints = n;
	trap_OAX_R_AddTrail( &out, pts );
}

/*
=================
CG_OAXTestTrail

cg_oaxTestTrail "shader x0 y0 z0 x1 y1 z1 width lifeMs" (cheat): a fixed
trail from the head (x0 y0 z0) to the tail, 16 points spread over its life,
every frame; it does not depend on any clock (render tests).
=================
*/
static void CG_OAXTestTrail( void ) {
	char shader[MAX_QPATH];
	char buf[MAX_CVAR_VALUE_STRING];
	char *p, *tok;
	float v[8], pts[16 * 4];
	oaxTrail_t t;
	int i;

	trap_Cvar_Update( &cg_oaxTestTrail );
	if ( !cg_oaxTestTrail.string[0] ) {
		return;
	}
	Q_strncpyz( buf, cg_oaxTestTrail.string, sizeof( buf ) );
	p = buf;
	Q_strncpyz( shader, COM_Parse( &p ), sizeof( shader ) );
	for ( i = 0; i < 8; i++ ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			return;
		}
		v[i] = atof( tok );
	}
	SetTrail( &t, trap_R_RegisterShader( shader ), v[6], v[6] * 0.25f, (int)v[7], 1, 0.6f, 0.2f, 1, 0.2f, 0.1f, 0.6f, 0, 0 );
	for ( i = 0; i < 16; i++ ) {
		float f = i / 15.0f;

		pts[i * 4 + 0] = v[0] + f * ( v[3] - v[0] );
		pts[i * 4 + 1] = v[1] + f * ( v[4] - v[1] );
		pts[i * 4 + 2] = v[2] + f * ( v[5] - v[2] );
		pts[i * 4 + 3] = f * v[7];
	}
	t.numPoints = 16;
	trap_OAX_R_AddTrail( &t, pts );
}

static void CG_OAXTrailsFrame( void ) {
	int i;
	oaxTrail_t style;

	trailFrame++;
	CG_OAXParseMapTrails();
	if ( !haveTrails || !cg_oaxTrails.integer || !cg.snap ) {
		return;
	}
	CG_OAXTestTrail();
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		centity_t *cent = &cg_entities[cg.snap->entities[i].number];

		if ( !TrailStyle( cent, &style ) || !style.shader ) {
			continue;
		}
		TrailUpdate( cent, &style );
	}
	/* free the trails of entities that are gone */
	for ( i = 0; i < MAX_CG_TRAILS; i++ ) {
		if ( trails[i].entity >= 0 && trails[i].seenFrame != trailFrame ) {
			trailSlot[trails[i].entity] = -1;
			trails[i].entity = -1;
		}
	}
}

/*
=================
Ground effects
=================
*/

/*
=================
CG_OAXWorldspawnValue

A worldspawn key's value, NULL if the map does not set it. The oax keys
(oax_*) are read once per map from the entity string's first entity (the
engine's parse point starts there after a map load); reading on to the
end rewinds that shared parse point for the next reader.
=================
*/
#define MAX_WS_KEYS 16
static int  wsParsed, wsNum;
static char wsKey[MAX_WS_KEYS][32], wsValue[MAX_WS_KEYS][MAX_QPATH];

const char *CG_OAXWorldspawnValue( const char *name ) {
	int i;

	if ( !wsParsed ) {
		char key[MAX_TOKEN_CHARS], value[MAX_TOKEN_CHARS];

		wsParsed = 1;
		wsNum = 0;
		if ( trap_GetEntityToken( key, sizeof( key ) ) && key[0] == '{' ) {
			while ( trap_GetEntityToken( key, sizeof( key ) ) && key[0] != '}' ) {
				if ( !trap_GetEntityToken( value, sizeof( value ) ) ) {
					break;
				}
				if ( !Q_stricmpn( key, "oax_", 4 ) && wsNum < MAX_WS_KEYS ) {
					Q_strncpyz( wsKey[wsNum], key, sizeof( wsKey[0] ) );
					Q_strncpyz( wsValue[wsNum], value, sizeof( wsValue[0] ) );
					wsNum++;
				}
			}
		}
		while ( trap_GetEntityToken( key, sizeof( key ) ) ) {
		}
	}
	for ( i = 0; i < wsNum; i++ ) {
		if ( !Q_stricmp( wsKey[i], name ) ) {
			return wsValue[i];
		}
	}
	return NULL;
}

static void CG_OAXGroundFxInit( void ) {
	int i;

	haveGroundFx = CG_OAXWorldspawnValue( "oax_groundfx" ) && atoi( CG_OAXWorldspawnValue( "oax_groundfx" ) );
	fxDust = fxRipple = fxSplash = 0;
	treadShader = 0;
	if ( haveGroundFx ) {
		if ( haveParticles ) {
			fxDust = RegisterFx( "oax/ground_dust" );
			fxRipple = RegisterFx( "oax/ripple" );
			fxSplash = RegisterFx( "oax/splash" );
		}
		if ( haveDecals ) {
			treadShader = trap_R_RegisterShader( "oaxfx/tread" );
		}
	}
	memset( rippleTime, 0, sizeof( rippleTime ) );
	for ( i = 0; i < CG_TRACK_SLOTS; i++ ) {
		tracks[i].entity = -1;
	}
	ripplesMade = tracksMade = 0;
	BG_OAXDebugSetInt( "cg_groundfx", haveGroundFx );
}

qboolean CG_OAXGroundFxOn( void ) {
	trap_Cvar_Update( &cg_oaxGroundFx );
	return haveGroundFx && cg_oaxGroundFx.integer;
}

/*
=================
CG_OAXWaterSurface

The water surface under (or at) p: a trace against water brushes from
above units over p down to below units under it. qfalse when there is no
water there, or p + above is already under water.
=================
*/
qboolean CG_OAXWaterSurface( const vec3_t p, float above, float below, vec3_t surface ) {
	trace_t tr;
	vec3_t start, end;

	VectorCopy( p, start );
	start[2] += above;
	VectorCopy( p, end );
	end[2] -= below;
	CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, CONTENTS_WATER );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, surface );
	return qtrue;
}

/* a ring on the water at surface (scale 1: a wading player's) */
void CG_OAXRipple( const vec3_t surface, float scale ) {
	vec3_t p, up = { 0, 0, 1 };

	VectorCopy( surface, p );
	p[2] += 0.75f;	/* over the surface, which writes depth */
	if ( CG_OAXSpawnFx( fxRipple, p, up, scale, NULL ) ) {
		ripplesMade++;
	}
}

void CG_OAXSplash( const vec3_t surface, float scale ) {
	vec3_t up = { 0, 0, 1 };

	CG_OAXSpawnFx( fxSplash, surface, up, scale, NULL );
}

void CG_OAXGroundDust( const vec3_t at, const vec3_t normal, float scale ) {
	vec3_t p;

	VectorMA( at, 6.0f, normal, p );	/* clear of the ground's soft-particle fade */
	CG_OAXSpawnFx( fxDust, p, normal, scale, NULL );
}

/*
=================
CG_OAXPlayerRipples

Players standing or wading in water: a ring every so often, more often
while they move.
=================
*/
static void PlayerRipple( int num, const vec3_t origin, const vec3_t velocity ) {
	vec3_t surface;
	float speed = sqrt( velocity[0] * velocity[0] + velocity[1] * velocity[1] );
	int interval = speed > 60.0f ? 240 : 1100;

	if ( cg.time - rippleTime[num] < interval && cg.time >= rippleTime[num] ) {
		return;
	}
	/* from about the eyes down to the feet (player boxes are -24..32) */
	if ( !CG_OAXWaterSurface( origin, 30.0f, 24.0f, surface ) ) {
		return;
	}
	rippleTime[num] = cg.time;
	CG_OAXRipple( surface, speed > 60.0f ? 1.0f : 0.6f );
	if ( speed > 250.0f ) {
		CG_OAXSplash( surface, 0.6f );
	}
}

static void CG_OAXPlayerRipples( void ) {
	int i;

	if ( !cg.snap || !fxRipple || !CG_OAXGroundFxOn() || !cg_oaxParticles.integer ) {
		return;
	}
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		centity_t *cent = &cg_entities[cg.snap->entities[i].number];

		if ( cent->currentState.eType != ET_PLAYER || ( cent->currentState.eFlags & EF_DEAD ) ) {
			continue;
		}
		PlayerRipple( cent->currentState.number, cent->lerpOrigin, cent->currentState.pos.trDelta );
	}
	if ( cg.predictedPlayerState.pm_type == PM_NORMAL && !CG_OAXVehicleDriving() ) {
		PlayerRipple( cg.predictedPlayerState.clientNum, cg.predictedPlayerState.origin,
			cg.predictedPlayerState.velocity );
	}
	BG_OAXDebugSetInt( "cg_ripples", ripplesMade );
	BG_OAXDebugSetInt( "cg_tracks", tracksMade );
}

/*
=================
CG_OAXTrack

A wheel of vehicle entity num touching the ground at contact (ground
normal n): every CG_TRACK_SEG units of travel since its last mark, a tyre
track decal from there to here. A jump of more than four segments (a
respawn, a long airborne stretch) starts over without a mark.
=================
*/
void CG_OAXTrack( int num, int wheel, const vec3_t contact, const vec3_t n, float width ) {
	cgTrack_t *t = NULL;
	oaxDecal_t d;
	vec3_t fwd, mid;
	float len;
	int i, oldest = 0;

	if ( !treadShader || !cg_oaxDecals.integer || wheel < 0 || wheel >= OAX_VEH_MAX_WHEELS ) {
		return;
	}
	for ( i = 0; i < CG_TRACK_SLOTS; i++ ) {
		if ( tracks[i].entity == num ) {
			t = &tracks[i];
			break;
		}
		if ( tracks[i].seenTime < tracks[oldest].seenTime ) {
			oldest = i;
		}
	}
	if ( !t ) {
		t = &tracks[oldest];
		memset( t, 0, sizeof( *t ) );
		t->entity = num;
	}
	t->seenTime = cg.time;
	if ( !t->have[wheel] ) {
		VectorCopy( contact, t->last[wheel] );
		t->have[wheel] = 1;
		return;
	}
	VectorSubtract( contact, t->last[wheel], fwd );
	len = VectorLength( fwd );
	if ( len < CG_TRACK_SEG ) {
		return;
	}
	VectorCopy( contact, t->last[wheel] );
	if ( len > CG_TRACK_SEG * 4 ) {
		return;
	}
	/* the box: axis 0 out of the ground, 1 across the tyre, 2 along it */
	memset( &d, 0, sizeof( d ) );
	VectorMA( contact, -0.5f, fwd, mid );
	VectorCopy( mid, d.origin );
	VectorNormalize2( n, d.axis[0] );
	VectorMA( fwd, -DotProduct( fwd, d.axis[0] ), d.axis[0], d.axis[2] );
	if ( VectorNormalize( d.axis[2] ) < 1.0f ) {
		return;
	}
	CrossProduct( d.axis[2], d.axis[0], d.axis[1] );
	d.halfSize[0] = 10;
	d.halfSize[1] = width * 0.5f;
	d.halfSize[2] = len * 0.5f + 1.0f;
	d.rgba[0] = 0.16f;
	d.rgba[1] = 0.13f;
	d.rgba[2] = 0.09f;
	d.rgba[3] = 0.8f;
	d.shader = treadShader;
	d.startTime = cg.time;
	d.lifeMs = CG_TRACK_LIFE;
	d.fadeMs = 3000;
	d.flags = OAXDECAL_ALPHAFADE;
	if ( trap_OAX_R_AddDecal( &d ) > 0 ) {
		tracksMade++;
		decalsMade++;
	}
}

/*
=================
CG_OAXFxFrame

After the scene's entities are added: the spawned particle systems and
the trails.
=================
*/
void CG_OAXFxFrame( void ) {
	int i, live = 0;

	trap_Cvar_Update( &cg_oaxParticles );
	trap_Cvar_Update( &cg_oaxDecals );
	trap_Cvar_Update( &cg_oaxTrails );
	CG_OAXEmitterDecls();

	if ( haveParticles ) {
		for ( i = 0; i < MAX_CG_FX; i++ ) {
			if ( !cgFx[i].inUse ) {
				continue;
			}
			if ( !trap_OAX_R_AddFx( &cgFx[i].fx ) ) {
				cgFx[i].inUse = 0;
				continue;
			}
			live++;
		}
	}
	CG_OAXTrailsFrame();
	CG_OAXPlayerRipples();

	BG_OAXDebugSetInt( "cg_fx_live", live );
	BG_OAXDebugSetInt( "cg_fx_spawned", fxSpawned );
	BG_OAXDebugSetInt( "cg_decals_made", decalsMade );
}

/*
=================
CG_OAXFxConsoleCommand
=================
*/
qboolean CG_OAXFxConsoleCommand( const char *cmd ) {
	vec3_t origin, dir;
	int i;

	/* a cvar set on the same console line is already in effect */
	trap_Cvar_Update( &cg_oaxParticles );
	trap_Cvar_Update( &cg_oaxDecals );

	if ( !Q_stricmp( cmd, "oaxfx" ) ) {
		int h;

		if ( trap_Argc() < 5 ) {
			CG_Printf( "usage: oaxfx <particle decl> x y z [dx dy dz]\n" );
			return qtrue;
		}
		for ( i = 0; i < 3; i++ ) {
			origin[i] = atof( CG_Argv( 2 + i ) );
			dir[i] = trap_Argc() >= 8 ? atof( CG_Argv( 5 + i ) ) : ( i == 2 ? 1 : 0 );
		}
		h = haveParticles ? RegisterFx( CG_Argv( 1 ) ) : 0;
		BG_OAXDebugSetInt( "cg_oaxfx_cmd", CG_OAXSpawnFx( h, origin, dir, 1.0f, NULL ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "oaxdecal" ) ) {
		if ( trap_Argc() < 9 ) {
			CG_Printf( "usage: oaxdecal <shader> x y z dx dy dz radius [angle [lifeMs [r g b]]]\n" );
			return qtrue;
		}
		for ( i = 0; i < 3; i++ ) {
			origin[i] = atof( CG_Argv( 2 + i ) );
			dir[i] = atof( CG_Argv( 5 + i ) );
		}
		{
			float c[3];

			for ( i = 0; i < 3; i++ ) {
				c[i] = trap_Argc() >= 14 ? atof( CG_Argv( 11 + i ) ) : 1;
			}
			BG_OAXDebugSetInt( "cg_oaxdecal_cmd", CG_OAXDecalLife( trap_R_RegisterShader( CG_Argv( 1 ) ), origin, dir,
				trap_Argc() >= 10 ? atof( CG_Argv( 9 ) ) : 0, c[0], c[1], c[2], 1, qfalse, atof( CG_Argv( 8 ) ),
				trap_Argc() >= 11 ? atoi( CG_Argv( 10 ) ) : 10000 ) );
		}
		return qtrue;
	}
	return qfalse;
}
