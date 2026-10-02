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
bg_oax_traj.c: evaluation of the oax mover trajectories (bg_oax_traj.h).

Adapted from DOOM-3 neo/idlib/math/Extrapolate.h (idExtrapolate, the
EXTRAPOLATION_ACCELLINEAR / LINEAR / DECELLINEAR / ACCELSINE / DECELSINE
cases), neo/idlib/math/Interpolate.h (idInterpolateAccelDecelLinear and
idInterpolateAccelDecelSine: Init and SetPhase) and
neo/game/physics/Physics_Parametric.cpp (Evaluate: the spline path with its
arc-length interpolation and spline angles).

Changes: C89 for the QVM instead of C++ templates; the three extrapolation
phases are computed in closed form from the time since the start (in ms,
relative, so large level times keep their precision) instead of re-initing
an idExtrapolate at phase changes; values are a scalar fraction of the
displacement times the displacement vector; trajectory parameters are packed
into a Quake III trajectory_t (see bg_oax_traj.h); GetTimeForLength's Newton
iteration is replaced by the arc-length table in bg_oax_spline.c.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_oax_traj.h"

#define OAX_SQRT_1OVER2 0.70710678118654752440f
#define OAX_HALF_PI     1.57079632679489661923f

int BG_OAXTrajIsOax( int trType ) {
	return trType >= TR_OAX_ACCELDECEL && trType <= TR_OAX_SPLINE_TANGENT;
}

int BG_OAXPackDuration( int durationMs, int accelMs, int decelMs ) {
	int aq, dq;

	if ( durationMs < 0 ) {
		durationMs = 0;
	}
	if ( durationMs > 65535 ) {
		durationMs = 65535;
	}
	if ( accelMs < 0 ) {
		accelMs = 0;
	}
	if ( decelMs < 0 ) {
		decelMs = 0;
	}
	if ( durationMs == 0 ) {
		return 0;
	}
	/* idInterpolateAccelDecel*::Init: scale accel and decel to fit */
	if ( accelMs + decelMs > durationMs ) {
		accelMs = accelMs * durationMs / ( accelMs + decelMs );
		decelMs = durationMs - accelMs;
	}
	aq = ( accelMs * 255 + durationMs / 2 ) / durationMs;
	dq = ( decelMs * 255 + durationMs / 2 ) / durationMs;
	if ( aq + dq > 255 ) {
		dq = 255 - aq;
	}
	return durationMs | ( aq << 16 ) | ( dq << 24 );
}

void BG_OAXUnpackDuration( int packed, float *duration, float *accel, float *decel ) {
	int d = packed & 0xffff;
	int aq = ( packed >> 16 ) & 255;
	int dq = ( packed >> 24 ) & 255;

	*duration = (float)d;
	*accel = (float)( aq * d ) / 255.0f;
	*decel = (float)( dq * d ) / 255.0f;
}

int BG_OAXTrajDuration( const trajectory_t *tr ) {
	if ( !BG_OAXTrajIsOax( tr->trType ) ) {
		return 0;
	}
	return tr->trDuration & 0xffff;
}

int BG_OAXTrajDone( const trajectory_t *tr, int atTime ) {
	if ( !BG_OAXTrajIsOax( tr->trType ) ) {
		return 0;
	}
	if ( ( tr->trType == TR_OAX_SPLINE || tr->trType == TR_OAX_SPLINE_TANGENT ) && tr->trBase[1] != 0 ) {
		return 0;	/* a loop never ends */
	}
	return atTime >= tr->trTime + ( tr->trDuration & 0xffff );
}

/*
=================
BG_OAXAccelDecel

idInterpolateAccelDecelLinear / Sine with start 0 and end 1: the fraction
of the move done dt ms after its start. *rate is d(fraction)/d(seconds).
=================
*/
float BG_OAXAccelDecel( float dt, float duration, float accel, float decel, int sine, float *rate ) {
	float linear, c, k, f, s;

	*rate = 0.0f;
	if ( duration <= 0.0f ) {
		return 1.0f;
	}
	if ( dt < 0.0f ) {
		return 0.0f;
	}
	linear = duration - accel - decel;
	c = sine ? OAX_SQRT_1OVER2 : 0.5f;
	/* speed = ( endValue - startValue ) * ( 1000 / ( linear + ( accel + decel ) * c ) ) */
	k = 1000.0f / ( linear + ( accel + decel ) * c );

	if ( dt < accel ) {
		/* EXTRAPOLATION_ACCELLINEAR / ACCELSINE over accel ms */
		f = dt / accel;
		if ( sine ) {
			s = ( 1.0f - cos( f * OAX_HALF_PI ) ) * accel * 0.001f * OAX_SQRT_1OVER2;
			*rate = k * sin( f * OAX_HALF_PI );
		} else {
			s = ( 0.5f * f * f ) * ( accel * 0.001f );
			*rate = k * f;
		}
		return s * k;
	}
	if ( dt < accel + linear ) {
		/* EXTRAPOLATION_LINEAR from startValue + speed * ( accel * 0.001 * c ) */
		*rate = k;
		return k * ( accel * 0.001f * c ) + ( dt - accel ) * 0.001f * k;
	}
	/* EXTRAPOLATION_DECELLINEAR / DECELSINE from endValue - speed * ( decel * 0.001 * c ) */
	if ( decel <= 0.0f ) {
		return 1.0f;
	}
	if ( dt >= duration ) {
		dt = duration;
	} else if ( sine ) {
		*rate = k * cos( ( dt - accel - linear ) / decel * OAX_HALF_PI );
	} else {
		*rate = k * ( 1.0f - ( dt - accel - linear ) / decel );
	}
	f = ( dt - accel - linear ) / decel;
	if ( sine ) {
		s = sin( f * OAX_HALF_PI ) * decel * 0.001f * OAX_SQRT_1OVER2;
	} else {
		s = ( f - ( 0.5f * f * f ) ) * ( decel * 0.001f );
	}
	return 1.0f - k * ( decel * 0.001f * c ) + s * k;
}

/* time into the move, wrapped for loops; returns the spline or NULL */
static const bgOaxSpline_t *BG_OAXSplineTime( const trajectory_t *tr, int atTime, float *len, float *lenRate ) {
	const bgOaxSpline_t *sp;
	float duration, accel, decel, frac, rate;
	int elapsed, d;

	sp = BG_OAXSpline( (int)tr->trBase[0] );
	if ( !sp ) {
		return NULL;
	}
	BG_OAXUnpackDuration( tr->trDuration, &duration, &accel, &decel );
	d = tr->trDuration & 0xffff;
	elapsed = atTime - tr->trTime;
	if ( tr->trBase[1] != 0 && d > 0 && elapsed >= d ) {
		elapsed = elapsed % d;
	}
	frac = BG_OAXAccelDecel( (float)elapsed, duration, accel, decel, 0, &rate );
	*len = frac * sp->total;
	*lenRate = rate * sp->total;
	return sp;
}

/*
=================
BG_OAXEvaluateTrajectory

qfalse if tr is not an oax type (the caller reports it).
=================
*/
qboolean BG_OAXEvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result ) {
	float duration, accel, decel, frac, rate, len, lenRate, u;
	const bgOaxSpline_t *sp;
	vec3_t v;

	switch ( (int)tr->trType ) {
	case TR_OAX_ACCELDECEL:
	case TR_OAX_ACCELDECEL_SINE:
		BG_OAXUnpackDuration( tr->trDuration, &duration, &accel, &decel );
		frac = BG_OAXAccelDecel( (float)( atTime - tr->trTime ), duration, accel, decel,
			tr->trType == TR_OAX_ACCELDECEL_SINE, &rate );
		VectorMA( tr->trBase, frac, tr->trDelta, result );
		return qtrue;
	case TR_OAX_SPLINE:
		sp = BG_OAXSplineTime( tr, atTime, &len, &lenRate );
		if ( !sp ) {
			VectorCopy( tr->trDelta, result );
			return qtrue;
		}
		u = BG_OAXSplineParamForLength( sp, len );
		BG_OAXSplinePoint( sp, u, v );
		VectorAdd( v, tr->trDelta, result );
		return qtrue;
	case TR_OAX_SPLINE_TANGENT:
		sp = BG_OAXSplineTime( tr, atTime, &len, &lenRate );
		if ( !sp ) {
			VectorCopy( tr->trDelta, result );
			return qtrue;
		}
		u = BG_OAXSplineParamForLength( sp, len );
		BG_OAXSplineDerivative( sp, u, v );
		BG_OAXVecToAngles( v, result );
		VectorAdd( result, tr->trDelta, result );
		return qtrue;
	}
	return qfalse;
}

/*
=================
BG_OAXEvaluateTrajectoryDelta

Velocity (units or degrees per second). Spline angles report zero.
=================
*/
qboolean BG_OAXEvaluateTrajectoryDelta( const trajectory_t *tr, int atTime, vec3_t result ) {
	float duration, accel, decel, rate, len, lenRate, u, speed;
	const bgOaxSpline_t *sp;
	vec3_t v;

	switch ( (int)tr->trType ) {
	case TR_OAX_ACCELDECEL:
	case TR_OAX_ACCELDECEL_SINE:
		BG_OAXUnpackDuration( tr->trDuration, &duration, &accel, &decel );
		BG_OAXAccelDecel( (float)( atTime - tr->trTime ), duration, accel, decel,
			tr->trType == TR_OAX_ACCELDECEL_SINE, &rate );
		VectorScale( tr->trDelta, rate, result );
		return qtrue;
	case TR_OAX_SPLINE:
		VectorClear( result );
		sp = BG_OAXSplineTime( tr, atTime, &len, &lenRate );
		if ( !sp ) {
			return qtrue;
		}
		u = BG_OAXSplineParamForLength( sp, len );
		BG_OAXSplineDerivative( sp, u, v );
		speed = sqrt( DotProduct( v, v ) );
		if ( speed > 0.0f ) {
			VectorScale( v, lenRate / speed, result );
		}
		return qtrue;
	case TR_OAX_SPLINE_TANGENT:
		VectorClear( result );
		return qtrue;
	}
	return qfalse;
}
