/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
/*
===========================================================================
g_oax_mover.c: keyframed and spline movers.

Adapted from DOOM-3 neo/game/Mover.cpp (idMover: Spawn defaults, BeginMove,
BeginRotation, DoneMoving, DoneRotating, Event_SetCallback,
Event_SetMoveSpeed, Event_SetMoveTime, Event_SetAccellerationTime,
Event_SetDecelerationTime, Event_MoveTo, Event_MoveToPos, Event_RotateTo,
Event_RotateOnce, Event_StartSpline, Event_StopSpline, Event_IsMoving,
Event_IsRotating) and neo/game/Entity.cpp (idEntity::GetSpline's curve
keys).

Changes: C89 in the Quake III game module. The idPhysics_Parametric staging
(Event_UpdateMove re-posting EXTRAPOLATION_* per stage) is replaced by one
packed accel/decel trajectory per move (bg_oax_traj.c), which the client
evaluates too, so movers predict; times are not snapped to physics frames;
pushing uses the stock G_MoverPush / G_MoverTeam; threads are numbers handed
to g_oaxMoverDone instead of idThread pointers; no save games.

func_oax_mover is new (keyframe movers, not from DOOM-3):

  key1 .. key7         offsets from the spawn position ("x y z"); key0 is 0 0 0
  key1_angles ..       angle offsets ("pitch yaw roll") at those keys
  movetime             seconds per key-to-key segment (default 1)
  glide                seconds of acceleration and of deceleration in each
                       segment (default 0); accel_time / decel_time override
  glide_sine           1: sinusoidal acceleration (AccelDecelSine)
  stay_open            seconds to wait at the last key (default 2)
  mode                 timed (default; use opens, waits, returns) | toggle |
                       bump (a player touching it opens it) | stand (a player
                       standing on it opens it) | trigger_control (use opens
                       it; it returns stay_open after the last use) | loop
                       (moves back and forth from the start, no input)
  encroach             stop (default; pause while blocked) | return (go back
                       to the previous key) | crush (damage the blocker)
  dmg                  crush damage (default 2)
  snd_start, snd_stop  sounds at the start and end of a segment
  noise                looping sound while moving

func_oax_splinemover:

  spline / target      targetname of an info_oax_spline, or of the first
                       path_corner of a chain (closed if it loops back)
  time | speed         seconds for one pass, or units per second (default
                       speed 100)
  accel, decel         seconds (D3 accel_time / decel_time)
  loop                 1: repeat forever
  spline_angles        1: face along the curve (D3 useSplineAngles)
  spline_absolute      1: the entity origin follows the curve points (D3);
                       default: the mover keeps its spawn offset from the
                       curve's first point
  start_off            1: wait for a use (or a script) to start

info_oax_spline: curve_CatmullRomSpline | curve_nubs "N ( x y z ... )"
(absolute points, as idEntity::GetSpline), closed 1 for a loop.

Debug values for tests: g_mover0_* (the func_oax_mover named "mover0"),
g_platform_* (the func_oax_splinemover named "platform"), and g_mover_hash /
g_mover_frames, an FNV-1a hash over every oax mover's origin and angles at
each server frame from 1 s to 21 s after the level start.
===========================================================================
*/
#include "g_local.h"
#include "bg_oax_traj.h"
#include "g_oax_mover.h"

#define OAXM_NONE    0
#define OAXM_KEYED   1
#define OAXM_SPLINE  2

#define MODE_TIMED   0
#define MODE_TOGGLE  1
#define MODE_BUMP    2
#define MODE_STAND   3
#define MODE_CONTROL 4
#define MODE_LOOP    5

#define ENC_STOP     0
#define ENC_RETURN   1
#define ENC_CRUSH    2

/* D3 moverCommand_t */
#define CMD_NONE     0
#define CMD_ROTATING 1
#define CMD_MOVING   2
#define CMD_SPLINE   3

/* keyed sequence state */
#define SEQ_IDLE     0
#define SEQ_MOVING   1
#define SEQ_WAITING  2

#define MAX_OAX_KEYS 8

typedef struct {
	int     kind;
	/* keyframes */
	int     numKeys;
	vec3_t  base, baseAngles;
	vec3_t  keyPos[MAX_OAX_KEYS];
	vec3_t  keyAng[MAX_OAX_KEYS];
	int     hasAngles;
	int     segTime, segAccel, segDecel, sine;
	int     stayOpen, mode, encroach;
	int     seq, key, from, goal, waitUntil;
	int     sndStart, sndStop, sndLoop;
	/* D3 idMover script state */
	float   moveSpeed;
	int     moveTime, accelTime, decelTime;
	vec3_t  destPos, destAng;
	int     moveThread, rotateThread, lastCommand;
	/* spline mover */
	char    *splineName;
	int     splineSlot, loop, splineAngles, splineAbsolute, startOff;
	float   splineTime, splineSpeed;
	/* test trace: hash of origin and angles per server frame, timed from
	   the mover's first move so player-started movers compare too */
	int     hashStart, hashRows;
	unsigned hash;
} oaxMover_t;

static oaxMover_t oaxMovers[MAX_GENTITIES];
static int        numSplineSlots;

void ( *g_oaxMoverDone )( gentity_t *ent, int threadNum );

void G_MoverTeam( gentity_t *ent );	/* g_mover.c */

static unsigned   moverHash;
static int        ridingSince;
static int        moverFrames;

static oaxMover_t *M( gentity_t *ent ) {
	oaxMover_t *m;

	if ( !ent || !ent->inuse || ent->s.eType != ET_MOVER || !ent->classname ) {
		return NULL;
	}
	m = &oaxMovers[ent->s.number];
	if ( m->kind == OAXM_NONE ) {
		return NULL;
	}
	if ( Q_stricmp( ent->classname, "func_oax_mover" ) && Q_stricmp( ent->classname, "func_oax_splinemover" ) ) {
		return NULL;
	}
	return m;
}

qboolean G_OAXIsMover( gentity_t *ent ) {
	return M( ent ) != NULL;
}

/* ---- trajectories ---------------------------------------------------------- */

static void SetStationary( trajectory_t *tr, const vec3_t value ) {
	tr->trType = TR_STATIONARY;
	tr->trTime = level.time;
	tr->trDuration = 0;
	VectorCopy( value, tr->trBase );
	VectorClear( tr->trDelta );
}

static void SetAccelDecel( trajectory_t *tr, int sine, const vec3_t from, const vec3_t to, int startTime, int durationMs, int accelMs, int decelMs ) {
	tr->trType = sine ? TR_OAX_ACCELDECEL_SINE : TR_OAX_ACCELDECEL;
	tr->trTime = startTime;
	tr->trDuration = BG_OAXPackDuration( durationMs, accelMs, decelMs );
	VectorCopy( from, tr->trBase );
	VectorSubtract( to, from, tr->trDelta );
}

static void CurrentOrigin( gentity_t *ent, vec3_t out ) {
	BG_EvaluateTrajectory( &ent->s.pos, level.time, out );
}

static void CurrentAngles( gentity_t *ent, vec3_t out ) {
	BG_EvaluateTrajectory( &ent->s.apos, level.time, out );
}

static void StartSound( gentity_t *ent, oaxMover_t *m ) {
	if ( m->sndStart ) {
		G_AddEvent( ent, EV_GENERAL_SOUND, m->sndStart );
	}
	ent->s.loopSound = m->sndLoop;
}

static void StopSound( gentity_t *ent, oaxMover_t *m ) {
	ent->s.loopSound = 0;
	if ( m->sndStop ) {
		G_AddEvent( ent, EV_GENERAL_SOUND, m->sndStop );
	}
}

/* ---- keyframed sequence (func_oax_mover) ----------------------------------- */

static void Seq_Segment( gentity_t *ent, oaxMover_t *m, int from, int to, int startTime ) {
	vec3_t a, b;

	m->seq = SEQ_MOVING;
	m->from = from;
	m->key = to;
	VectorAdd( m->base, m->keyPos[from], a );
	VectorAdd( m->base, m->keyPos[to], b );
	SetAccelDecel( &ent->s.pos, m->sine, a, b, startTime, m->segTime, m->segAccel, m->segDecel );
	if ( m->hasAngles ) {
		VectorAdd( m->baseAngles, m->keyAng[from], a );
		VectorAdd( m->baseAngles, m->keyAng[to], b );
		SetAccelDecel( &ent->s.apos, m->sine, a, b, startTime, m->segTime, m->segAccel, m->segDecel );
	}
	StartSound( ent, m );
}

static void Seq_GoTo( gentity_t *ent, oaxMover_t *m, int goal, int startTime ) {
	m->goal = goal;
	m->lastCommand = CMD_NONE;
	if ( m->seq == SEQ_MOVING ) {
		return;	/* continues toward the new goal from the next key */
	}
	if ( m->key == goal ) {
		return;
	}
	Seq_Segment( ent, m, m->key, m->key + ( goal > m->key ? 1 : -1 ), startTime );
}

static void Seq_Arrive( gentity_t *ent, oaxMover_t *m, int endTime ) {
	vec3_t p;

	VectorAdd( m->base, m->keyPos[m->key], p );
	SetStationary( &ent->s.pos, p );
	ent->s.pos.trTime = endTime;
	if ( m->hasAngles ) {
		VectorAdd( m->baseAngles, m->keyAng[m->key], p );
		SetStationary( &ent->s.apos, p );
		ent->s.apos.trTime = endTime;
	}
	StopSound( ent, m );
	m->seq = SEQ_IDLE;

	switch ( m->mode ) {
	case MODE_LOOP:
		m->seq = SEQ_WAITING;
		m->waitUntil = endTime + m->stayOpen;
		m->goal = ( m->key == 0 ) ? m->numKeys - 1 : 0;
		break;
	case MODE_TOGGLE:
		break;
	default:
		if ( m->key == m->numKeys - 1 ) {
			m->seq = SEQ_WAITING;
			m->waitUntil = endTime + m->stayOpen;
			m->goal = 0;
		}
		break;
	}
}

/* advance the keyed sequence up to level.time; segments chain on their exact end times */
static void Seq_Run( gentity_t *ent, oaxMover_t *m ) {
	int guard, end;

	for ( guard = 0; guard < 16; guard++ ) {
		if ( m->seq == SEQ_MOVING ) {
			end = ent->s.pos.trTime + BG_OAXTrajDuration( &ent->s.pos );
			if ( level.time < end ) {
				return;
			}
			if ( m->key == m->goal ) {
				Seq_Arrive( ent, m, end );
				if ( m->seq == SEQ_WAITING ) {
					continue;
				}
				return;
			}
			Seq_Segment( ent, m, m->key, m->key + ( m->goal > m->key ? 1 : -1 ), end );
		} else if ( m->seq == SEQ_WAITING ) {
			if ( level.time < m->waitUntil ) {
				return;
			}
			m->seq = SEQ_IDLE;
			Seq_GoTo( ent, m, m->goal, m->waitUntil );
		} else {
			return;
		}
	}
}

static void Seq_Open( gentity_t *ent, oaxMover_t *m ) {
	if ( m->kind != OAXM_KEYED || m->numKeys < 2 ) {
		return;
	}
	switch ( m->mode ) {
	case MODE_TOGGLE:
		Seq_GoTo( ent, m, m->goal == m->numKeys - 1 ? 0 : m->numKeys - 1, level.time );
		break;
	case MODE_CONTROL:
		if ( m->seq == SEQ_WAITING && m->goal == 0 ) {
			m->waitUntil = level.time + m->stayOpen;
			return;
		}
		/* fall through */
	default:
		if ( m->seq == SEQ_IDLE && m->key == 0 ) {
			Seq_GoTo( ent, m, m->numKeys - 1, level.time );
		}
		break;
	}
}

static void Use_OAXMover( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	oaxMover_t *m = M( ent );

	if ( m ) {
		Seq_Open( ent, m );
	}
}

static void Touch_OAXMover( gentity_t *ent, gentity_t *other, trace_t *trace ) {
	oaxMover_t *m = M( ent );

	if ( m && m->mode == MODE_BUMP && other->client ) {
		Seq_Open( ent, m );
	}
}

static void Blocked_OAXMover( gentity_t *ent, gentity_t *other ) {
	oaxMover_t *m = M( ent );
	vec3_t org;
	int elapsed;

	if ( !m ) {
		return;
	}
	if ( m->encroach == ENC_CRUSH ) {
		if ( !other->client ) {
			if ( other->s.eType == ET_ITEM && other->item && other->item->giType == IT_TEAM ) {
				Team_DroppedFlagThink( other );
				return;
			}
			G_TempEntity( other->s.origin, EV_ITEM_POP );
			G_FreeEntity( other );
			return;
		}
		if ( ent->damage ) {
			G_Damage( other, ent, ent, NULL, NULL, ent->damage, 0, MOD_CRUSH );
		}
		return;
	}
	if ( m->encroach == ENC_RETURN && m->kind == OAXM_KEYED && m->seq == SEQ_MOVING ) {
		/* go back to the key the segment started from */
		CurrentOrigin( ent, org );
		elapsed = level.time - ent->s.pos.trTime;
		if ( elapsed < 50 ) {
			elapsed = 50;
		}
		{
			int from = m->key;
			vec3_t b;
			VectorAdd( m->base, m->keyPos[m->from], b );
			SetAccelDecel( &ent->s.pos, m->sine, org, b, level.time, elapsed, 0, 0 );
			if ( m->hasAngles ) {
				vec3_t a;
				CurrentAngles( ent, a );
				VectorAdd( m->baseAngles, m->keyAng[m->from], b );
				SetAccelDecel( &ent->s.apos, m->sine, a, b, level.time, elapsed, 0, 0 );
			}
			m->key = m->from;
			m->from = from;
			m->goal = m->key;
		}
	}
	/* ENC_STOP: G_MoverTeam has already backed the team up; it retries next frame */
}

/* ---- D3 idMover script interface -------------------------------------------- */

static void DoneMoving( gentity_t *ent, oaxMover_t *m ) {
	vec3_t org;
	int thread;

	if ( ent->s.pos.trType != TR_OAX_SPLINE ) {
		/* set our final position so that we get rid of any numerical inaccuracy */
		SetStationary( &ent->s.pos, m->destPos );
	} else {
		CurrentOrigin( ent, org );
		SetStationary( &ent->s.pos, org );
	}
	m->lastCommand = CMD_NONE;
	thread = m->moveThread;
	m->moveThread = 0;
	StopSound( ent, m );
	if ( thread && g_oaxMoverDone ) {
		g_oaxMoverDone( ent, thread );
	}
}

static void DoneRotating( gentity_t *ent, oaxMover_t *m ) {
	int thread;

	m->lastCommand = CMD_NONE;
	thread = m->rotateThread;
	m->rotateThread = 0;
	StopSound( ent, m );
	if ( thread && g_oaxMoverDone ) {
		g_oaxMoverDone( ent, thread );
	}
}

static void BeginMove( gentity_t *ent, oaxMover_t *m ) {
	vec3_t org, delta;
	float dist, acceldist;
	int at, dt, totalacceltime;

	m->seq = SEQ_IDLE;	/* a script move cancels the keyframe sequence */
	m->lastCommand = CMD_MOVING;
	m->moveThread = 0;

	CurrentOrigin( ent, org );
	VectorSubtract( m->destPos, org, delta );
	if ( VectorCompare( delta, vec3_origin ) ) {
		DoneMoving( ent, m );
		return;
	}

	/* if we're moving at a specific speed, we need to calculate the move time */
	if ( m->moveSpeed ) {
		dist = VectorLength( delta );
		totalacceltime = m->accelTime + m->decelTime;
		/* calculate the distance we'll move during acceleration and deceleration */
		acceldist = totalacceltime * 0.5f * 0.001f * m->moveSpeed;
		if ( acceldist >= dist ) {
			/* going too slow for this distance to move at a constant speed */
			m->moveTime = totalacceltime;
		} else {
			/* calculate move time taking acceleration into account */
			m->moveTime = totalacceltime + 1000.0f * ( dist - acceldist ) / m->moveSpeed;
		}
	}
	if ( m->moveTime < 1 ) {
		m->moveTime = 1;
	}

	at = m->accelTime;
	dt = m->decelTime;
	if ( at + dt > m->moveTime ) {
		/* scale the times to fit into the move time in the same proportions */
		at = at * m->moveTime / ( at + dt );
		dt = m->moveTime - at;
	}
	SetAccelDecel( &ent->s.pos, 0, org, m->destPos, level.time, m->moveTime, at, dt );
	StartSound( ent, m );
}

static void BeginRotation( gentity_t *ent, oaxMover_t *m ) {
	vec3_t ang, delta;
	int at, dt, i;

	m->seq = SEQ_IDLE;
	m->lastCommand = CMD_ROTATING;
	m->rotateThread = 0;

	/* rotation always uses move_time; with no time set, it takes 1 ms */
	if ( !m->moveTime ) {
		m->moveTime = 1;
	}
	CurrentAngles( ent, ang );
	VectorSubtract( m->destAng, ang, delta );
	if ( VectorCompare( delta, vec3_origin ) ) {
		for ( i = 0; i < 3; i++ ) {
			m->destAng[i] = AngleNormalize360( m->destAng[i] );
		}
		SetStationary( &ent->s.apos, m->destAng );
		DoneRotating( ent, m );
		return;
	}
	at = m->accelTime;
	dt = m->decelTime;
	if ( at + dt > m->moveTime ) {
		at = at * m->moveTime / ( at + dt );
		dt = m->moveTime - at;
	}
	SetAccelDecel( &ent->s.apos, 0, ang, m->destAng, level.time, m->moveTime, at, dt );
	StartSound( ent, m );
}

void G_OAXMoverSetSpeed( gentity_t *ent, float speed ) {
	oaxMover_t *m = M( ent );

	if ( !m || speed <= 0 ) {
		return;
	}
	m->moveSpeed = speed;
	m->moveTime = 0;	/* move_time is calculated for each move when move_speed is non-0 */
}

void G_OAXMoverSetTime( gentity_t *ent, float seconds ) {
	oaxMover_t *m = M( ent );

	if ( !m || seconds <= 0 ) {
		return;
	}
	m->moveSpeed = 0;
	m->moveTime = seconds * 1000.0f;
}

void G_OAXMoverSetAccelTime( gentity_t *ent, float seconds ) {
	oaxMover_t *m = M( ent );

	if ( m && seconds >= 0 ) {
		m->accelTime = seconds * 1000.0f;
	}
}

void G_OAXMoverSetDecelTime( gentity_t *ent, float seconds ) {
	oaxMover_t *m = M( ent );

	if ( m && seconds >= 0 ) {
		m->decelTime = seconds * 1000.0f;
	}
}

void G_OAXMoverMoveToPos( gentity_t *ent, const vec3_t pos ) {
	oaxMover_t *m = M( ent );

	if ( !m ) {
		return;
	}
	VectorCopy( pos, m->destPos );
	BeginMove( ent, m );
}

void G_OAXMoverMoveTo( gentity_t *ent, gentity_t *target ) {
	oaxMover_t *m = M( ent );

	if ( !m || !target ) {
		return;
	}
	VectorCopy( target->r.currentOrigin, m->destPos );
	if ( VectorCompare( m->destPos, vec3_origin ) ) {
		VectorCopy( target->s.origin, m->destPos );
	}
	BeginMove( ent, m );
}

void G_OAXMoverRotateTo( gentity_t *ent, const vec3_t angles ) {
	oaxMover_t *m = M( ent );

	if ( !m ) {
		return;
	}
	VectorCopy( angles, m->destAng );
	BeginRotation( ent, m );
}

void G_OAXMoverRotateOnce( gentity_t *ent, const vec3_t delta ) {
	oaxMover_t *m = M( ent );
	vec3_t ang;

	if ( !m ) {
		return;
	}
	if ( m->rotateThread ) {
		DoneRotating( ent, m );
	}
	CurrentAngles( ent, ang );
	VectorAdd( ang, delta, m->destAng );
	BeginRotation( ent, m );
}

int G_OAXMoverIsMoving( gentity_t *ent ) {
	oaxMover_t *m = M( ent );

	return m && ent->s.pos.trType != TR_STATIONARY;
}

int G_OAXMoverIsRotating( gentity_t *ent ) {
	oaxMover_t *m = M( ent );

	return m && ent->s.apos.trType != TR_STATIONARY;
}

int G_OAXMoverSetCallback( gentity_t *ent, int threadNum ) {
	oaxMover_t *m = M( ent );

	if ( !m ) {
		return 0;
	}
	if ( m->lastCommand == CMD_ROTATING && !m->rotateThread ) {
		m->lastCommand = CMD_NONE;
		m->rotateThread = threadNum;
		return 1;
	}
	if ( ( m->lastCommand == CMD_MOVING || m->lastCommand == CMD_SPLINE ) && !m->moveThread ) {
		m->lastCommand = CMD_NONE;
		m->moveThread = threadNum;
		return 1;
	}
	return 0;
}

/* ---- splines ----------------------------------------------------------------- */

static int SetSplineConfigstring( const char *cs ) {
	int slot;

	if ( numSplineSlots >= OAX_MAX_SPLINES ) {
		G_Printf( "oax: more than %i splines\n", OAX_MAX_SPLINES );
		return -1;
	}
	slot = numSplineSlots;
	trap_SetConfigstring( CS_OAX_SPLINES + slot, cs );
	if ( !BG_OAXSplineParse( slot, cs ) ) {
		trap_SetConfigstring( CS_OAX_SPLINES + slot, "" );
		return -1;
	}
	numSplineSlots++;
	return slot;
}

static void AppendPoint( char *cs, int size, const vec3_t p ) {
	Q_strcat( cs, size, va( " %.3f %.3f %.3f", p[0], p[1], p[2] ) );
}

/* info_oax_spline: curve_CatmullRomSpline / curve_nubs "N ( x y z ... )" */
static int SplineSlotFromCurve( gentity_t *ent ) {
	char cs[MAX_STRING_CHARS];
	char buf[MAX_STRING_CHARS];
	char *p;
	int n, i, k, closed;
	vec3_t v;
	oaxMover_t *m = &oaxMovers[ent->s.number];

	if ( m->splineSlot >= 0 ) {
		return m->splineSlot;
	}
	if ( !m->splineName ) {
		return -1;
	}
	Q_strncpyz( buf, m->splineName + 1, sizeof( buf ) );
	closed = m->loop;
	p = buf;
	n = atoi( COM_Parse( &p ) );
	if ( n < 2 || n > OAX_SPLINE_MAX_POINTS ) {
		G_Printf( "info_oax_spline: bad point count %i\n", n );
		return -1;
	}
	if ( Q_stricmp( COM_Parse( &p ), "(" ) ) {
		return -1;
	}
	Com_sprintf( cs, sizeof( cs ), "%c %i %i", m->splineName[0], closed, n );
	for ( i = 0; i < n; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			v[k] = atof( COM_Parse( &p ) );
		}
		AppendPoint( cs, sizeof( cs ), v );
	}
	m->splineSlot = SetSplineConfigstring( cs );
	return m->splineSlot;
}

/* a path_corner chain starting at targetname name: closed if it loops back */
static int SplineSlotFromCorners( const char *name ) {
	gentity_t *path, *next, *start;
	vec3_t pts[OAX_SPLINE_MAX_POINTS];
	char cs[MAX_STRING_CHARS];
	int n, closed, i;

	start = G_Find( NULL, FOFS( targetname ), name );
	if ( !start || Q_stricmp( start->classname, "path_corner" ) ) {
		return -1;
	}
	n = 0;
	closed = 0;
	for ( path = start; path && n < OAX_SPLINE_MAX_POINTS; path = next ) {
		VectorCopy( path->s.origin, pts[n] );
		n++;
		next = NULL;
		if ( path->target ) {
			do {
				next = G_Find( next, FOFS( targetname ), path->target );
			} while ( next && Q_stricmp( next->classname, "path_corner" ) );
		}
		if ( next == start ) {
			closed = 1;
			break;
		}
	}
	if ( n < 2 ) {
		return -1;
	}
	Com_sprintf( cs, sizeof( cs ), "c %i %i", closed, n );
	for ( i = 0; i < n; i++ ) {
		AppendPoint( cs, sizeof( cs ), pts[i] );
	}
	return SetSplineConfigstring( cs );
}

static int SplineSlotFor( gentity_t *splineEnt ) {
	if ( !splineEnt ) {
		return -1;
	}
	if ( !Q_stricmp( splineEnt->classname, "info_oax_spline" ) ) {
		return SplineSlotFromCurve( splineEnt );
	}
	if ( !Q_stricmp( splineEnt->classname, "path_corner" ) && splineEnt->targetname ) {
		oaxMover_t *m = &oaxMovers[splineEnt->s.number];
		if ( m->splineSlot < 0 ) {
			m->splineSlot = SplineSlotFromCorners( splineEnt->targetname );
		}
		return m->splineSlot;
	}
	return -1;
}

static void StartSplineSlot( gentity_t *ent, oaxMover_t *m, int slot, int loop, const vec3_t offset ) {
	const bgOaxSpline_t *sp = BG_OAXSpline( slot );
	int at, dt, time;

	if ( !sp ) {
		return;
	}
	m->seq = SEQ_IDLE;
	m->lastCommand = CMD_SPLINE;
	m->moveThread = 0;

	time = m->moveTime;
	if ( m->moveSpeed > 0 ) {
		time = sp->total * 1000.0f / m->moveSpeed;
	}
	if ( time < 1 ) {
		time = 1;
	}
	at = m->accelTime;
	dt = m->decelTime;
	if ( at + dt > time ) {
		at = time / 2;
		dt = time - at;
	}
	ent->s.pos.trType = TR_OAX_SPLINE;
	ent->s.pos.trTime = level.time;
	ent->s.pos.trDuration = BG_OAXPackDuration( time, at, dt );
	VectorSet( ent->s.pos.trBase, slot, loop, 0 );
	VectorCopy( offset, ent->s.pos.trDelta );
	if ( m->splineAngles ) {
		ent->s.apos = ent->s.pos;
		ent->s.apos.trType = TR_OAX_SPLINE_TANGENT;
		VectorCopy( m->baseAngles, ent->s.apos.trDelta );
	}
	StartSound( ent, m );
}

void G_OAXMoverStartSpline( gentity_t *ent, gentity_t *splineEnt ) {
	oaxMover_t *m = M( ent );
	int slot;

	if ( !m ) {
		return;
	}
	slot = SplineSlotFor( splineEnt );
	if ( slot < 0 ) {
		return;
	}
	/* D3: the entity origin follows the curve */
	StartSplineSlot( ent, m, slot, 0, vec3_origin );
}

void G_OAXMoverStopSpline( gentity_t *ent ) {
	oaxMover_t *m = M( ent );
	vec3_t v;

	if ( !m ) {
		return;
	}
	if ( ent->s.pos.trType == TR_OAX_SPLINE ) {
		CurrentOrigin( ent, v );
		SetStationary( &ent->s.pos, v );
	}
	if ( ent->s.apos.trType == TR_OAX_SPLINE_TANGENT ) {
		CurrentAngles( ent, v );
		SetStationary( &ent->s.apos, v );
	}
	if ( m->lastCommand == CMD_SPLINE ) {
		m->lastCommand = CMD_NONE;
	}
}

/* func_oax_splinemover's own path, started at init or on use */
static void SplineMover_Start( gentity_t *ent, oaxMover_t *m ) {
	gentity_t *target;
	int slot;
	const bgOaxSpline_t *sp;
	vec3_t first, offset;

	target = m->splineName ? G_Find( NULL, FOFS( targetname ), m->splineName ) : NULL;
	while ( target && Q_stricmp( target->classname, "info_oax_spline" ) && Q_stricmp( target->classname, "path_corner" ) ) {
		target = G_Find( target, FOFS( targetname ), m->splineName );
	}
	slot = SplineSlotFor( target );
	sp = BG_OAXSpline( slot );
	if ( !sp ) {
		G_Printf( "func_oax_splinemover: no spline '%s'\n", m->splineName ? m->splineName : "" );
		return;
	}
	VectorClear( offset );
	if ( !m->splineAbsolute ) {
		BG_OAXSplinePoint( sp, 0, first );
		VectorSubtract( m->base, first, offset );
	}
	if ( m->splineTime > 0 ) {
		m->moveSpeed = 0;
		m->moveTime = m->splineTime * 1000.0f;
	} else {
		m->moveSpeed = m->splineSpeed > 0 ? m->splineSpeed : 100;
	}
	StartSplineSlot( ent, m, slot, m->loop, offset );
}

static void Use_OAXSplineMover( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	oaxMover_t *m = M( ent );

	/* D3 Event_Activate starts the spline; a running one keeps going */
	if ( m && ent->s.pos.trType == TR_STATIONARY ) {
		SplineMover_Start( ent, m );
	}
}

/* ---- spawning ------------------------------------------------------------------ */

static void InitOAXMover( gentity_t *ent, oaxMover_t *m ) {
	char *s;
	float f, light;
	vec3_t color;
	qboolean lightSet, colorSet;

	if ( ent->model2 ) {
		ent->s.modelindex2 = G_ModelIndex( ent->model2 );
	}
	trap_SetBrushModel( ent, ent->model );

	if ( G_SpawnString( "noise", "", &s ) && s[0] ) {
		m->sndLoop = G_SoundIndex( s );
	}
	if ( G_SpawnString( "snd_start", "", &s ) && s[0] ) {
		m->sndStart = G_SoundIndex( s );
	}
	if ( G_SpawnString( "snd_stop", "", &s ) && s[0] ) {
		m->sndStop = G_SoundIndex( s );
	}

	lightSet = G_SpawnFloat( "light", "100", &light );
	colorSet = G_SpawnVector( "color", "1 1 1", color );
	if ( lightSet || colorSet ) {
		int r, g, b, i;

		r = color[0] * 255;
		if ( r > 255 ) {
			r = 255;
		}
		g = color[1] * 255;
		if ( g > 255 ) {
			g = 255;
		}
		b = color[2] * 255;
		if ( b > 255 ) {
			b = 255;
		}
		i = light / 4;
		if ( i > 255 ) {
			i = 255;
		}
		ent->s.constantLight = r | ( g << 8 ) | ( b << 16 ) | ( i << 24 );
	}

	/* D3 idMover::Spawn script defaults */
	G_SpawnFloat( "move_time", "1", &f );
	m->moveTime = f * 1000.0f;
	G_SpawnFloat( "move_speed", "0", &f );
	m->moveSpeed = f;
	G_SpawnFloat( "accel_time", "0", &f );
	m->accelTime = f * 1000.0f;
	G_SpawnFloat( "decel_time", "0", &f );
	m->decelTime = f * 1000.0f;
	G_SpawnInt( "dmg", "2", &ent->damage );

	VectorCopy( ent->s.origin, m->base );
	VectorCopy( ent->s.angles, m->baseAngles );
	VectorCopy( m->base, m->destPos );
	VectorCopy( m->baseAngles, m->destAng );
	m->splineSlot = -1;
	m->hashStart = -1;

	ent->blocked = Blocked_OAXMover;
	ent->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	ent->s.eType = ET_MOVER;
	VectorCopy( m->base, ent->r.currentOrigin );
	VectorCopy( m->baseAngles, ent->r.currentAngles );
	SetStationary( &ent->s.pos, m->base );
	SetStationary( &ent->s.apos, m->baseAngles );
	trap_LinkEntity( ent );
}

void SP_func_oax_mover( gentity_t *ent ) {
	oaxMover_t *m = &oaxMovers[ent->s.number];
	char *s;
	float f;
	int i;

	memset( m, 0, sizeof( *m ) );
	m->kind = OAXM_KEYED;
	InitOAXMover( ent, m );

	m->numKeys = 1;
	for ( i = 1; i < MAX_OAX_KEYS; i++ ) {
		if ( !G_SpawnVector( va( "key%i", i ), "0 0 0", m->keyPos[i] ) ) {
			break;
		}
		if ( G_SpawnVector( va( "key%i_angles", i ), "0 0 0", m->keyAng[i] ) ) {
			m->hasAngles = 1;
		}
		m->numKeys = i + 1;
	}
	G_SpawnFloat( "movetime", "1", &f );
	m->segTime = f * 1000.0f;
	if ( m->segTime < 1 ) {
		m->segTime = 1;
	}
	G_SpawnFloat( "glide", "0", &f );
	m->segAccel = m->segDecel = f * 1000.0f;
	if ( G_SpawnFloat( "accel_time", "0", &f ) ) {
		m->segAccel = f * 1000.0f;
	}
	if ( G_SpawnFloat( "decel_time", "0", &f ) ) {
		m->segDecel = f * 1000.0f;
	}
	G_SpawnInt( "glide_sine", "0", &m->sine );
	G_SpawnFloat( "stay_open", "2", &f );
	m->stayOpen = f * 1000.0f;

	G_SpawnString( "mode", "timed", &s );
	if ( !Q_stricmp( s, "toggle" ) ) {
		m->mode = MODE_TOGGLE;
	} else if ( !Q_stricmp( s, "bump" ) ) {
		m->mode = MODE_BUMP;
	} else if ( !Q_stricmp( s, "stand" ) ) {
		m->mode = MODE_STAND;
	} else if ( !Q_stricmp( s, "trigger_control" ) ) {
		m->mode = MODE_CONTROL;
	} else if ( !Q_stricmp( s, "loop" ) ) {
		m->mode = MODE_LOOP;
	} else {
		m->mode = MODE_TIMED;
	}
	G_SpawnString( "encroach", "stop", &s );
	if ( !Q_stricmp( s, "return" ) ) {
		m->encroach = ENC_RETURN;
	} else if ( !Q_stricmp( s, "crush" ) ) {
		m->encroach = ENC_CRUSH;
	} else {
		m->encroach = ENC_STOP;
	}
	if ( !m->sndStart && !m->sndStop && !G_SpawnString( "snd_start", "", &s ) ) {
		m->sndStart = G_SoundIndex( "sound/movers/doors/dr1_strt.wav" );
		m->sndStop = G_SoundIndex( "sound/movers/doors/dr1_end.wav" );
	}

	ent->use = Use_OAXMover;
	ent->touch = Touch_OAXMover;
}

void SP_func_oax_splinemover( gentity_t *ent ) {
	oaxMover_t *m = &oaxMovers[ent->s.number];
	char *s;

	memset( m, 0, sizeof( *m ) );
	m->kind = OAXM_SPLINE;
	InitOAXMover( ent, m );

	if ( G_SpawnString( "spline", "", &s ) && s[0] ) {
		m->splineName = G_NewString( s );
	} else {
		m->splineName = ent->target;
	}
	G_SpawnFloat( "time", "0", &m->splineTime );
	G_SpawnFloat( "speed", "100", &m->splineSpeed );
	{
		float f;
		G_SpawnFloat( "accel", "0", &f );
		m->accelTime = f * 1000.0f;
		G_SpawnFloat( "decel", "0", &f );
		m->decelTime = f * 1000.0f;
	}
	G_SpawnInt( "loop", "0", &m->loop );
	G_SpawnInt( "spline_angles", "0", &m->splineAngles );
	G_SpawnInt( "spline_absolute", "0", &m->splineAbsolute );
	G_SpawnInt( "start_off", "0", &m->startOff );
	ent->use = Use_OAXSplineMover;
}

void SP_info_oax_spline( gentity_t *ent ) {
	oaxMover_t *m = &oaxMovers[ent->s.number];
	char *s;
	int closed;

	memset( m, 0, sizeof( *m ) );
	m->splineSlot = -1;
	G_SpawnInt( "closed", "0", &closed );
	m->loop = closed;
	/* keep "<type char><curve string>" until G_OAXMoverInit assigns slots */
	if ( G_SpawnString( "curve_CatmullRomSpline", "", &s ) && s[0] ) {
		m->splineName = G_NewString( va( "c%s", s ) );
	} else if ( G_SpawnString( "curve_nubs", "", &s ) && s[0] ) {
		m->splineName = G_NewString( va( "n%s", s ) );
	} else {
		G_Printf( "info_oax_spline without curve_CatmullRomSpline or curve_nubs\n" );
	}
}

/* ---- per-frame --------------------------------------------------------------- */

void G_OAXMoverInit( void ) {
	int i;
	gentity_t *ent;
	oaxMover_t *m;

	numSplineSlots = 0;
	moverHash = 2166136261u;
	moverFrames = 0;
	ridingSince = -1;
	for ( i = 0; i < OAX_MAX_SPLINES; i++ ) {
		BG_OAXSplineClear( i );
		trap_SetConfigstring( CS_OAX_SPLINES + i, "" );
	}
	/* path_corner slots are cached per entity: reset them */
	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		if ( ent->inuse && ent->classname && !Q_stricmp( ent->classname, "path_corner" ) ) {
			memset( &oaxMovers[i], 0, sizeof( oaxMovers[i] ) );
			oaxMovers[i].splineSlot = -1;
		}
	}
	/* curves first, in entity order, so slot numbers are stable */
	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		if ( ent->inuse && ent->classname && !Q_stricmp( ent->classname, "info_oax_spline" ) ) {
			SplineSlotFromCurve( ent );
		}
	}
	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		m = M( ent );
		if ( !m ) {
			continue;
		}
		if ( m->kind == OAXM_SPLINE && !m->startOff ) {
			SplineMover_Start( ent, m );
		} else if ( m->kind == OAXM_KEYED && m->mode == MODE_LOOP && m->numKeys > 1 ) {
			Seq_GoTo( ent, m, m->numKeys - 1, level.time );
		}
		if ( ent->targetname && !Q_stricmp( ent->targetname, "mover0" ) ) {
			BG_OAXDebugSetInt( "g_mover0_start", level.time );
			BG_OAXDebugSetInt( "g_mover0_ent", i );
		}
		if ( ent->targetname && !Q_stricmp( ent->targetname, "platform" ) ) {
			BG_OAXDebugSetInt( "g_platform_ent", i );
			BG_OAXDebugSetInt( "g_platform_start", level.time );
		}
	}
	BG_OAXDebugSetInt( "g_level_start", level.startTime );
}

static unsigned HashInt( unsigned h, int v ) {
	return ( h ^ (unsigned)v ) * 16777619u;
}

static unsigned HashFloat( unsigned h, float f ) {
	union { float f; int i; } u;

	u.f = f;
	return HashInt( h, u.i );
}

static void StandCheck( gentity_t *ent, oaxMover_t *m ) {
	int i;
	gclient_t *cl;

	for ( i = 0; i < level.maxclients; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected == CON_CONNECTED && cl->ps.groundEntityNum == ent->s.number ) {
			Seq_Open( ent, m );
			return;
		}
	}
}

void G_OAXMoverRunFrame( void ) {
	int i, rel;
	gentity_t *ent;
	oaxMover_t *m;
	vec3_t org, ang;

	rel = level.time - level.startTime;
	moverHash = 2166136261u;
	moverFrames = 0;
	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		m = M( ent );
		if ( !m ) {
			continue;
		}
		if ( m->kind == OAXM_KEYED && m->mode == MODE_STAND && m->seq == SEQ_IDLE ) {
			StandCheck( ent, m );
		}
		if ( m->seq != SEQ_IDLE ) {
			Seq_Run( ent, m );
		} else {
			if ( BG_OAXTrajDone( &ent->s.pos, level.time ) && ( m->lastCommand == CMD_MOVING || m->lastCommand == CMD_SPLINE || m->moveThread ) ) {
				DoneMoving( ent, m );
			} else if ( BG_OAXTrajDone( &ent->s.pos, level.time ) ) {
				CurrentOrigin( ent, org );
				SetStationary( &ent->s.pos, org );
				StopSound( ent, m );
			}
			if ( BG_OAXTrajDone( &ent->s.apos, level.time ) ) {
				/* rotateTo stops at its destination */
				int k;
				if ( ent->s.apos.trType == TR_OAX_SPLINE_TANGENT ) {
					CurrentAngles( ent, ang );
				} else {
					for ( k = 0; k < 3; k++ ) {
						ang[k] = AngleNormalize360( m->destAng[k] );
					}
				}
				SetStationary( &ent->s.apos, ang );
				DoneRotating( ent, m );
			}
		}

		CurrentOrigin( ent, org );
		CurrentAngles( ent, ang );
		/* a move that just ended left the collision origin one frame short:
		   G_RunMover skips stationary movers, so settle it here (with pushing) */
		if ( ent->s.pos.trType == TR_STATIONARY && ent->s.apos.trType == TR_STATIONARY &&
			( !VectorCompare( org, ent->r.currentOrigin ) || !VectorCompare( ang, ent->r.currentAngles ) ) ) {
			G_MoverTeam( ent );
		}
		if ( m->hashStart < 0 ) {
			m->hash = 2166136261u;
			m->hashRows = 0;
			if ( ent->s.pos.trType != TR_STATIONARY ) {
				m->hashStart = ent->s.pos.trTime;
			} else if ( ent->s.apos.trType != TR_STATIONARY ) {
				m->hashStart = ent->s.apos.trTime;
			}
		}
		if ( m->hashStart >= 0 && level.time - m->hashStart >= 1000 && level.time - m->hashStart <= 21000 ) {
			m->hash = HashInt( m->hash, level.time - m->hashStart );
			m->hash = HashFloat( m->hash, org[0] );
			m->hash = HashFloat( m->hash, org[1] );
			m->hash = HashFloat( m->hash, org[2] );
			m->hash = HashFloat( m->hash, ang[0] );
			m->hash = HashFloat( m->hash, ang[1] );
			m->hash = HashFloat( m->hash, ang[2] );
			m->hashRows++;
		}
		moverHash = HashInt( HashInt( HashInt( moverHash, i ), (int)m->hash ), m->hashRows );
		moverFrames += m->hashRows;
		if ( ent->targetname && !Q_stricmp( ent->targetname, "mover0" ) ) {
			BG_OAXDebugSet( "g_mover0_origin", va( "%.4f %.4f %.4f", org[0], org[1], org[2] ) );
			BG_OAXDebugSet( "g_mover0_angles", va( "%.4f %.4f %.4f", ang[0], ang[1], ang[2] ) );
			BG_OAXDebugSetInt( "g_mover0_time", level.time );
		}
		if ( ent->targetname && !Q_stricmp( ent->targetname, "platform" ) ) {
			BG_OAXDebugSet( "g_platform_origin", va( "%.4f %.4f %.4f", org[0], org[1], org[2] ) );
			if ( level.maxclients > 0 && level.clients[0].pers.connected == CON_CONNECTED ) {
				int ground = level.clients[0].ps.groundEntityNum;
				BG_OAXDebugSetInt( "g_client0_ground", ground );
				if ( ground != i ) {
					ridingSince = -1;
				} else if ( ridingSince < 0 ) {
					ridingSince = level.time;
				}
				BG_OAXDebugSetInt( "g_platform_riding", ridingSince < 0 ? 0 : level.time - ridingSince );
			}
		}
	}
	BG_OAXDebugSet( "g_mover_hash", va( "%08x", moverHash ) );
	BG_OAXDebugSetInt( "g_mover_frames", moverFrames );
	BG_OAXDebugSetInt( "g_level_rel", rel );
}
