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
bg_oax_spline.c: CatmullRom and non-uniform B-spline curves for the oax
movers, with an arc-length table (bg_oax_traj.h).

Adapted from DOOM-3 neo/idlib/math/Curve.h: idCurve (IndexForTime,
GetSpeed, RombergIntegral), idCurve_Spline (ValueForIndex, TimeForIndex,
ClampedTime for BT_CLAMPED and BT_CLOSED), idCurve_CatmullRomSpline
(GetCurrentValue, GetCurrentFirstDerivative, Basis, BasisFirstDerivative)
and idCurve_NonUniformBSpline (the same, order 4); and from
neo/game/Entity.cpp idEntity::GetSpline (the curve string format) and
neo/idlib/math/Vector.cpp idVec3::ToAngles.

Changes: C89 for the QVM instead of C++ templates; vectors only; knots are
always uniform (knot i at parameter i, as GetSpline's 100 ms spacing after
MakeUniform), so TimeForIndex is the index itself in every boundary mode;
the curve comes from a configstring rather than spawn args; and
GetTimeForLength's per-call Romberg plus Newton iteration is replaced by a
64-entry arc-length table built once per spline (Romberg integrals of order
5, as GetLengthBetweenKnots uses) and inverted by linear interpolation.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_oax_traj.h"

static bgOaxSpline_t oaxSplines[OAX_MAX_SPLINES];

const bgOaxSpline_t *BG_OAXSpline( int slot ) {
	if ( slot < 0 || slot >= OAX_MAX_SPLINES || !oaxSplines[slot].valid ) {
		return NULL;
	}
	return &oaxSplines[slot];
}

void BG_OAXSplineClear( int slot ) {
	if ( slot >= 0 && slot < OAX_MAX_SPLINES ) {
		oaxSplines[slot].valid = 0;
	}
}

/* idCurve_Spline::ValueForIndex */
static void ValueForIndex( const bgOaxSpline_t *sp, int index, vec3_t out ) {
	int n = sp->numPoints - 1;
	int m = sp->numPoints;
	vec3_t d;

	if ( index < 0 ) {
		if ( sp->closed ) {
			VectorCopy( sp->points[( ( index % m ) + m ) % m], out );
		} else {
			VectorSubtract( sp->points[1], sp->points[0], d );
			VectorMA( sp->points[0], (float)index, d, out );
		}
	} else if ( index > n ) {
		if ( sp->closed ) {
			VectorCopy( sp->points[index % m], out );
		} else {
			VectorSubtract( sp->points[n], sp->points[n - 1], d );
			VectorMA( sp->points[n], (float)( index - n ), d, out );
		}
	} else {
		VectorCopy( sp->points[index], out );
	}
}

/* idCurve_Spline::TimeForIndex: with uniform knots, the index itself */
#define TimeForIndex( sp, index ) ( (float)( index ) )

/* idCurve_Spline::ClampedTime */
static float ClampedTime( const bgOaxSpline_t *sp, float t ) {
	if ( !sp->closed ) {
		if ( t < 0.0f ) {
			return 0.0f;
		}
		if ( t >= sp->numPoints - 1 ) {
			return (float)( sp->numPoints - 1 );
		}
	}
	return t;
}

/* idCurve::IndexForTime: the first knot index whose time is >= t */
static int IndexForTime( const bgOaxSpline_t *sp, float t ) {
	int i;

	if ( t <= 0.0f ) {
		return 0;
	}
	if ( t > sp->numPoints - 1 ) {
		return sp->numPoints;
	}
	i = (int)t;
	if ( (float)i < t ) {
		i++;
	}
	return i;
}

/* idCurve_CatmullRomSpline::Basis */
static void CRBasis( int index, float t, float *bvals ) {
	float s = (float)( t - TimeForIndex( sp, index ) ) / ( TimeForIndex( sp, index + 1 ) - TimeForIndex( sp, index ) );

	bvals[0] = ( ( -s + 2.0f ) * s - 1.0f ) * s * 0.5f;
	bvals[1] = ( ( ( 3.0f * s - 5.0f ) * s ) * s + 2.0f ) * 0.5f;
	bvals[2] = ( ( -3.0f * s + 4.0f ) * s + 1.0f ) * s * 0.5f;
	bvals[3] = ( ( s - 1.0f ) * s * s ) * 0.5f;
}

/* idCurve_CatmullRomSpline::BasisFirstDerivative */
static void CRBasisFirstDerivative( int index, float t, float *bvals ) {
	float s = (float)( t - TimeForIndex( sp, index ) ) / ( TimeForIndex( sp, index + 1 ) - TimeForIndex( sp, index ) );

	bvals[0] = ( -1.5f * s + 2.0f ) * s - 0.5f;
	bvals[1] = ( 4.5f * s - 5.0f ) * s;
	bvals[2] = ( -4.5f * s + 4.0f ) * s + 0.5f;
	bvals[3] = 1.5f * s * s - s;
}

/* idCurve_NonUniformBSpline::Basis */
static void NUBasis( int index, int order, float t, float *bvals ) {
	int r, s, i;
	float omega;

	bvals[order - 1] = 1.0f;
	for ( r = 2; r <= order; r++ ) {
		i = index - r + 1;
		bvals[order - r] = 0.0f;
		for ( s = order - r + 1; s < order; s++ ) {
			i++;
			omega = (float)( t - TimeForIndex( sp, i ) ) / ( TimeForIndex( sp, i + r - 1 ) - TimeForIndex( sp, i ) );
			bvals[s - 1] += ( 1.0f - omega ) * bvals[s];
			bvals[s] *= omega;
		}
	}
}

/* idCurve_NonUniformBSpline::BasisFirstDerivative */
static void NUBasisFirstDerivative( int index, int order, float t, float *bvals ) {
	int i;

	NUBasis( index, order - 1, t, bvals + 1 );
	bvals[0] = 0.0f;
	for ( i = 0; i < order - 1; i++ ) {
		bvals[i] -= bvals[i + 1];
		bvals[i] *= (float)( order - 1 ) / ( TimeForIndex( sp, index + i + ( order - 1 ) - 2 ) - TimeForIndex( sp, index + i - 2 ) );
	}
	bvals[i] *= (float)( order - 1 ) / ( TimeForIndex( sp, index + i + ( order - 1 ) - 2 ) - TimeForIndex( sp, index + i - 2 ) );
}

static void SplineEval( const bgOaxSpline_t *sp, float time, int derivative, vec3_t out ) {
	int i, j, k;
	float bvals[4], clampedTime;
	vec3_t p;

	VectorClear( out );
	if ( sp->numPoints == 1 ) {
		if ( !derivative ) {
			VectorCopy( sp->points[0], out );
		}
		return;
	}
	clampedTime = ClampedTime( sp, time );
	i = IndexForTime( sp, clampedTime );
	if ( sp->type == 'n' ) {
		if ( derivative ) {
			NUBasisFirstDerivative( i - 1, 4, clampedTime, bvals );
		} else {
			NUBasis( i - 1, 4, clampedTime, bvals );
		}
	} else {
		if ( derivative ) {
			CRBasisFirstDerivative( i - 1, clampedTime, bvals );
		} else {
			CRBasis( i - 1, clampedTime, bvals );
		}
	}
	for ( j = 0; j < 4; j++ ) {
		k = i + j - 2;
		ValueForIndex( sp, k, p );
		VectorMA( out, bvals[j], p, out );
	}
	if ( derivative && sp->type != 'n' ) {
		/* CatmullRom divides by the span length, which is 1 here */
		float d = TimeForIndex( sp, i ) - TimeForIndex( sp, i - 1 );
		VectorScale( out, 1.0f / d, out );
	}
}

void BG_OAXSplinePoint( const bgOaxSpline_t *sp, float u, vec3_t out ) {
	SplineEval( sp, u, 0, out );
}

void BG_OAXSplineDerivative( const bgOaxSpline_t *sp, float u, vec3_t out ) {
	SplineEval( sp, u, 1, out );
}

/* idCurve::GetSpeed */
static float GetSpeed( const bgOaxSpline_t *sp, float t ) {
	vec3_t v;

	SplineEval( sp, t, 1, v );
	return sqrt( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );
}

/* idCurve::RombergIntegral */
static float RombergIntegral( const bgOaxSpline_t *sp, float t0, float t1, int order ) {
	int i, j, k, m, n;
	float sum, delta;
	float temp[2][8];

	delta = t1 - t0;
	temp[0][0] = 0.5f * delta * ( GetSpeed( sp, t0 ) + GetSpeed( sp, t1 ) );

	for ( i = 2, m = 1; i <= order; i++, m *= 2, delta *= 0.5f ) {
		/* approximate using the trapezoid rule */
		sum = 0.0f;
		for ( j = 1; j <= m; j++ ) {
			sum += GetSpeed( sp, t0 + delta * ( j - 0.5f ) );
		}
		/* Richardson extrapolation */
		temp[1][0] = 0.5f * ( temp[0][0] + delta * sum );
		for ( k = 1, n = 4; k < i; k++, n *= 4 ) {
			temp[1][k] = ( n * temp[1][k - 1] - temp[0][k - 1] ) / ( n - 1 );
		}
		for ( j = 0; j < i; j++ ) {
			temp[0][j] = temp[1][j];
		}
	}
	return temp[0][order - 1];
}

/* replaces idCurve::GetTimeForLength */
float BG_OAXSplineParamForLength( const bgOaxSpline_t *sp, float len ) {
	int lo, hi, mid;
	float step, span;

	if ( len <= 0.0f ) {
		return 0.0f;
	}
	if ( len >= sp->total ) {
		return sp->umax;
	}
	/* the last table entry with length <= len */
	lo = 0;
	hi = OAX_SPLINE_TABLE - 1;
	while ( hi - lo > 1 ) {
		mid = ( lo + hi ) >> 1;
		if ( sp->length[mid] <= len ) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	step = sp->umax / (float)( OAX_SPLINE_TABLE - 1 );
	span = sp->length[hi] - sp->length[lo];
	if ( span <= 0.0f ) {
		return step * lo;
	}
	return step * lo + ( len - sp->length[lo] ) / span * step;
}

/* idVec3::ToAngles */
void BG_OAXVecToAngles( const vec3_t v, vec3_t angles ) {
	float forward, yaw, pitch;

	if ( v[0] == 0.0f && v[1] == 0.0f ) {
		yaw = 0.0f;
		if ( v[2] > 0.0f ) {
			pitch = 90.0f;
		} else {
			pitch = 270.0f;
		}
	} else {
		yaw = atan2( v[1], v[0] ) * ( 180.0f / M_PI );
		if ( yaw < 0.0f ) {
			yaw += 360.0f;
		}
		forward = sqrt( v[0] * v[0] + v[1] * v[1] );
		pitch = atan2( v[2], forward ) * ( 180.0f / M_PI );
		if ( pitch < 0.0f ) {
			pitch += 360.0f;
		}
	}
	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0.0f;
}

/*
=================
BG_OAXSplineParse

"<type> <closed> <n> x y z ..." (bg_oax_traj.h). Builds the arc-length
table. An empty or bad string clears the slot.
=================
*/
int BG_OAXSplineParse( int slot, const char *cs ) {
	bgOaxSpline_t *sp;
	char *p, *tok;
	int i, k;
	char buf[MAX_STRING_CHARS];
	float step;

	if ( slot < 0 || slot >= OAX_MAX_SPLINES ) {
		return 0;
	}
	sp = &oaxSplines[slot];
	sp->valid = 0;
	if ( !cs || !cs[0] ) {
		return 0;
	}
	Q_strncpyz( buf, cs, sizeof( buf ) );
	p = buf;
	tok = COM_Parse( &p );
	sp->type = ( tok[0] == 'n' ) ? 'n' : 'c';
	sp->closed = atoi( COM_Parse( &p ) ) != 0;
	sp->numPoints = atoi( COM_Parse( &p ) );
	if ( sp->numPoints < 2 || sp->numPoints > OAX_SPLINE_MAX_POINTS ) {
		return 0;
	}
	for ( i = 0; i < sp->numPoints; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			tok = COM_Parse( &p );
			if ( !tok[0] ) {
				return 0;
			}
			sp->points[i][k] = atof( tok );
		}
	}
	sp->umax = (float)( sp->closed ? sp->numPoints : sp->numPoints - 1 );
	step = sp->umax / (float)( OAX_SPLINE_TABLE - 1 );
	sp->length[0] = 0.0f;
	for ( i = 1; i < OAX_SPLINE_TABLE; i++ ) {
		sp->length[i] = sp->length[i - 1] + RombergIntegral( sp, step * ( i - 1 ), step * i, 5 );
	}
	sp->total = sp->length[OAX_SPLINE_TABLE - 1];
	sp->valid = 1;
	return 1;
}
