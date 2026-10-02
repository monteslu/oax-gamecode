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
bg_oax_traj.h: oax mover trajectories and splines, shared by game and cgame.

Adapted from DOOM-3 neo/idlib/math/Extrapolate.h, neo/idlib/math/Interpolate.h,
neo/idlib/math/Curve.h and neo/game/physics/Physics_Parametric.cpp: the
accel/decel interpolation and the CatmullRom / non-uniform B-spline curves,
ported by hand from C++ templates to C89 for the QVM (see bg_oax_traj.c and
bg_oax_spline.c for what changed).

Trajectory encoding (a trajectory_t is self-contained: callers pass only the
trajectory, so every parameter lives in it):

  TR_OAX_ACCELDECEL, TR_OAX_ACCELDECEL_SINE (pos or apos)
    trBase     start value
    trDelta    total displacement (end - start)
    trTime     start time (ms)
    trDuration packed: duration ms (bits 0-15, max 65535)
               | accelQ << 16 | decelQ << 24
               accel ms = accelQ * duration / 255, decel likewise,
               accelQ + decelQ <= 255
    Evaluated as idInterpolateAccelDecelLinear / idInterpolateAccelDecelSine:
    accelerate, constant speed, decelerate, then hold the end value.

  TR_OAX_SPLINE (pos)
    trBase[0]  spline slot (0 .. OAX_MAX_SPLINES-1)
    trBase[1]  1 = loop (wrap the time modulo the duration), 0 = stop at end
    trDelta    offset added to the spline point
    trTime, trDuration as above; the accel/decel phases act on the arc length
    (Physics_Parametric's splineInterpolate), so the speed along the curve is
    constant between them.

  TR_OAX_SPLINE_TANGENT (apos)
    Same fields as TR_OAX_SPLINE; the angles face along the curve
    (idVec3::ToAngles of the first derivative) plus trDelta.

Spline configstrings CS_OAX_SPLINES + slot (one spline per slot):

  "<type> <closed> <n> x0 y0 z0 x1 y1 z1 ..."

  type    c = CatmullRom (idCurve_CatmullRomSpline), n = non-uniform
          B-spline of order 4 (idCurve_NonUniformBSpline)
  closed  0 = BT_CLAMPED (as idEntity::GetSpline), 1 = BT_CLOSED (a loop)
  n       number of points (2 .. OAX_SPLINE_MAX_POINTS)

Knots are uniform (knot i at parameter i, idEntity::GetSpline's 100 ms
spacing after MakeUniform); a closed curve has one more span back to the
first point. Game and cgame both parse the same string, so both build the
same 64-entry arc-length table.
===========================================================================
*/
#ifndef BG_OAX_TRAJ_H
#define BG_OAX_TRAJ_H

#define TR_OAX_ACCELDECEL       32
#define TR_OAX_ACCELDECEL_SINE  33
#define TR_OAX_SPLINE           34
#define TR_OAX_SPLINE_TANGENT   35

#define OAX_MAX_SPLINES         64
#define OAX_SPLINE_MAX_POINTS   40
#define OAX_SPLINE_TABLE        64

typedef struct {
	int     valid;
	int     type;            /* 'c' or 'n' */
	int     closed;
	int     numPoints;
	vec3_t  points[OAX_SPLINE_MAX_POINTS];
	float   umax;            /* parameter range [0, umax] */
	float   length[OAX_SPLINE_TABLE];   /* arc length at u = umax * k / (OAX_SPLINE_TABLE - 1) */
	float   total;
} bgOaxSpline_t;

/* trajectories (bg_oax_traj.c) */
int      BG_OAXPackDuration( int durationMs, int accelMs, int decelMs );
void     BG_OAXUnpackDuration( int packed, float *duration, float *accel, float *decel );
int      BG_OAXTrajDuration( const trajectory_t *tr );            /* ms, 0 if not an oax type */
int      BG_OAXTrajIsOax( int trType );
int      BG_OAXTrajDone( const trajectory_t *tr, int atTime );    /* a finite move that has ended */
/* fraction of the displacement covered dt ms into a move (0..1), and its
   rate in 1/s; sine selects the AccelDecelSine profile */
float    BG_OAXAccelDecel( float dt, float duration, float accel, float decel, int sine, float *rate );
qboolean BG_OAXEvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result );
qboolean BG_OAXEvaluateTrajectoryDelta( const trajectory_t *tr, int atTime, vec3_t result );

/* splines (bg_oax_spline.c) */
int      BG_OAXSplineParse( int slot, const char *cs );    /* 1 if the slot now holds a valid spline */
void     BG_OAXSplineClear( int slot );
const bgOaxSpline_t *BG_OAXSpline( int slot );          /* NULL if empty */
void     BG_OAXSplinePoint( const bgOaxSpline_t *sp, float u, vec3_t out );
void     BG_OAXSplineDerivative( const bgOaxSpline_t *sp, float u, vec3_t out );
float    BG_OAXSplineParamForLength( const bgOaxSpline_t *sp, float len );
void     BG_OAXVecToAngles( const vec3_t v, vec3_t angles );  /* idVec3::ToAngles */

#endif
