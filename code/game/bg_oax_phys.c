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
bg_oax_phys.c: physics helpers shared by the game and the cgame.

Defaults for the physics defs (a zeroed def means gravity scale 0, no
friction and an identity rotation, which is rarely what anyone wants),
quaternion helpers, and the stock ragdoll human the determinism scene and
MD3-free fallbacks use. Everything here is QVM float math, so the game
gets the same bits on every build.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_oax_phys.h"

void BG_PhysWorldDefInit( oaxPhysWorldDef_t *def, float gravity ) {
	memset( def, 0, sizeof( *def ) );
	def->gravity[2] = -gravity;
	def->workerCount = 1;
	def->tickMsec = 16;
	def->substeps = 4;
}

void BG_PhysBodyDefInit( oaxPhysBodyDef_t *def, int type ) {
	memset( def, 0, sizeof( *def ) );
	def->type = type;
	def->quat[3] = 1.0f;
	def->gravityScale = 1.0f;
}

void BG_PhysShapeDefInit( oaxPhysShapeDef_t *def, int type, float density ) {
	memset( def, 0, sizeof( *def ) );
	def->type = type;
	def->quat[3] = 1.0f;
	def->density = density;
	def->friction = 0.6f;
}

/* rotation matrix (Q3 axis: rows are the forward, left, up vectors) to a
   quaternion x y z w */
void BG_QuatFromAxis( vec3_t axis[3], float *q ) {
	/* m[r][c] with columns = axis vectors */
	float m00 = axis[0][0], m01 = axis[1][0], m02 = axis[2][0];
	float m10 = axis[0][1], m11 = axis[1][1], m12 = axis[2][1];
	float m20 = axis[0][2], m21 = axis[1][2], m22 = axis[2][2];
	float t = m00 + m11 + m22, s;

	if ( t > 0.0f ) {
		s = 0.5f / sqrt( t + 1.0f );
		q[3] = 0.25f / s;
		q[0] = ( m21 - m12 ) * s;
		q[1] = ( m02 - m20 ) * s;
		q[2] = ( m10 - m01 ) * s;
	} else if ( m00 > m11 && m00 > m22 ) {
		s = 2.0f * sqrt( 1.0f + m00 - m11 - m22 );
		q[3] = ( m21 - m12 ) / s;
		q[0] = 0.25f * s;
		q[1] = ( m01 + m10 ) / s;
		q[2] = ( m02 + m20 ) / s;
	} else if ( m11 > m22 ) {
		s = 2.0f * sqrt( 1.0f + m11 - m00 - m22 );
		q[3] = ( m02 - m20 ) / s;
		q[0] = ( m01 + m10 ) / s;
		q[1] = 0.25f * s;
		q[2] = ( m12 + m21 ) / s;
	} else {
		s = 2.0f * sqrt( 1.0f + m22 - m00 - m11 );
		q[3] = ( m10 - m01 ) / s;
		q[0] = ( m02 + m20 ) / s;
		q[1] = ( m12 + m21 ) / s;
		q[2] = 0.25f * s;
	}
}

void BG_QuatToAxis( const float *q, vec3_t axis[3] ) {
	float x = q[0], y = q[1], z = q[2], w = q[3];

	axis[0][0] = 1.0f - 2.0f * ( y * y + z * z );
	axis[0][1] = 2.0f * ( x * y + w * z );
	axis[0][2] = 2.0f * ( x * z - w * y );
	axis[1][0] = 2.0f * ( x * y - w * z );
	axis[1][1] = 1.0f - 2.0f * ( x * x + z * z );
	axis[1][2] = 2.0f * ( y * z + w * x );
	axis[2][0] = 2.0f * ( x * z + w * y );
	axis[2][1] = 2.0f * ( y * z - w * x );
	axis[2][2] = 1.0f - 2.0f * ( x * x + y * y );
}

void BG_QuatMul( const float *a, const float *b, float *out ) {
	float r[4];

	r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	out[0] = r[0];
	out[1] = r[1];
	out[2] = r[2];
	out[3] = r[3];
}

void BG_QuatRotate( const float *q, const vec3_t v, vec3_t out ) {
	vec3_t axis[3];

	BG_QuatToAxis( q, axis );
	/* axis rows are the rotated basis vectors */
	out[0] = axis[0][0] * v[0] + axis[1][0] * v[1] + axis[2][0] * v[2];
	out[1] = axis[0][1] * v[0] + axis[1][1] * v[1] + axis[2][1] * v[2];
	out[2] = axis[0][2] * v[0] + axis[1][2] * v[1] + axis[2][2] * v[2];
}

void BG_QuatFromAngles( const vec3_t angles, float *quat ) {
	vec3_t axis[3];

	AnglesToAxis( angles, axis );
	BG_QuatFromAxis( axis, quat );
}

/*
==================
BG_PhysHumanBones

Bones in Quake units for a 56-unit player facing +x, left along +y, then
scaled, turned by yaw and moved to origin (the feet). Elbows bend forward
(negative hinge angles about +y), knees backward (positive).
==================
*/
typedef struct {
	int		parent;
	float	head[3], tail[3];
	float	radius;
	int		joint;
	float	axis[3];
	float	cone, lower, upper;
	float	friction;
} physHumanBone_t;

static const physHumanBone_t humanBones[PHYS_HUMAN_BONES] = {
	/* 0 pelvis */	{ -1, { 0, 0, 26 }, { 0, 0, 32 }, 6.0f, PHYS_RAGDOLL_BALL, { 0, 0, 0 }, 0, 0, 0, 0 },
	/* 1 chest */	{ 0, { 0, 0, 33 }, { 0, 0, 45 }, 7.0f, PHYS_RAGDOLL_BALL, { 0, 0, 0 }, 0.5f, -0.4f, 0.4f, 60 },
	/* 2 head */	{ 1, { 0, 0, 47 }, { 0, 0, 54 }, 4.5f, PHYS_RAGDOLL_BALL, { 0, 0, 0 }, 0.6f, -0.7f, 0.7f, 20 },
	/* 3 arm L */	{ 1, { 0, 9, 44 }, { 0, 9, 34 }, 2.6f, PHYS_RAGDOLL_BALL, { 0, 0, 0 }, 1.4f, -1.0f, 1.0f, 20 },
	/* 4 fore L */	{ 3, { 0, 9, 34 }, { 3, 9, 24 }, 2.3f, PHYS_RAGDOLL_HINGE, { 0, 1, 0 }, 0, -2.5f, 0.05f, 10 },
	/* 5 arm R */	{ 1, { 0, -9, 44 }, { 0, -9, 34 }, 2.6f, PHYS_RAGDOLL_BALL, { 0, 0, 0 }, 1.4f, -1.0f, 1.0f, 20 },
	/* 6 fore R */	{ 5, { 0, -9, 34 }, { 3, -9, 24 }, 2.3f, PHYS_RAGDOLL_HINGE, { 0, 1, 0 }, 0, -2.5f, 0.05f, 10 },
	/* 7 thigh L */	{ 0, { 0, 4, 25 }, { 0, 4, 14 }, 3.6f, PHYS_RAGDOLL_BALL, { 0, 0, 0 }, 1.1f, -0.5f, 0.5f, 40 },
	/* 8 shin L */	{ 7, { 0, 4, 14 }, { 0, 4, 3 }, 3.0f, PHYS_RAGDOLL_HINGE, { 0, 1, 0 }, 0, -0.05f, 2.4f, 20 },
	/* 9 thigh R */	{ 0, { 0, -4, 25 }, { 0, -4, 14 }, 3.6f, PHYS_RAGDOLL_BALL, { 0, 0, 0 }, 1.1f, -0.5f, 0.5f, 40 },
	/* 10 shin R */	{ 9, { 0, -4, 14 }, { 0, -4, 3 }, 3.0f, PHYS_RAGDOLL_HINGE, { 0, 1, 0 }, 0, -0.05f, 2.4f, 20 }
};

static void BG_PhysHumanPoint( const float *p, const vec3_t origin, float c, float s, float scale, float *out ) {
	out[0] = origin[0] + scale * ( p[0] * c - p[1] * s );
	out[1] = origin[1] + scale * ( p[0] * s + p[1] * c );
	out[2] = origin[2] + scale * p[2];
}

void BG_PhysHumanBones( const vec3_t origin, float yaw, float scale, oaxPhysRagdollBone_t *bones ) {
	float c = cos( DEG2RAD( yaw ) ), s = sin( DEG2RAD( yaw ) );
	vec3_t zero;
	int i;

	VectorClear( zero );
	for ( i = 0; i < PHYS_HUMAN_BONES; i++ ) {
		const physHumanBone_t *h = &humanBones[i];
		oaxPhysRagdollBone_t *b = &bones[i];
		memset( b, 0, sizeof( *b ) );
		b->parent = h->parent;
		BG_PhysHumanPoint( h->head, origin, c, s, scale, b->head );
		BG_PhysHumanPoint( h->tail, origin, c, s, scale, b->tail );
		BG_PhysHumanPoint( h->axis, zero, c, s, 1.0f, b->axis );
		b->radius = h->radius * scale;
		b->density = 900.0f;
		b->jointType = h->joint;
		b->coneAngle = h->cone;
		b->lower = h->lower;
		b->upper = h->upper;
		b->friction = h->friction * scale * scale;
	}
}
