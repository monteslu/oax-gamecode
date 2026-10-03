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
bg_oax_phys.h: the physics syscalls as gamecode calls them, and helpers
shared by the game and the cgame (bg_oax_phys.c).

Engine side: code/physics in the oax engine
(github.com/monteslu/oax-engine), Box3D worlds. ABI: oax_phys.h.
Every trap needs BG_OAXFeature("physics") first; the skeleton traps (cgame
only) need "physics_skel".
===========================================================================
*/
#ifndef BG_OAX_PHYS_H
#define BG_OAX_PHYS_H

#include "oax_phys.h"

/* worlds */
int		trap_Phys_WorldCreate( const oaxPhysWorldDef_t *def );
void	trap_Phys_WorldDestroy( int world );
int		trap_Phys_WorldStep( int world, int msec );
int		trap_Phys_WorldAddBSP( int world, int contentsMask, int flags, const oaxPhysShapeDef_t *material );
int		trap_Phys_WorldAddHeightField( int world, const oaxPhysHeightField_t *hf, const float *heights, const oaxPhysShapeDef_t *material );
void	trap_Phys_WorldSetGravity( int world, const vec3_t gravity );
int		trap_Phys_WorldStats( int world, oaxPhysStats_t *out );
int		trap_Phys_WorldHash( int world );
void	trap_Phys_WorldExplode( int world, const vec3_t origin, float radius, float falloff, float impulsePerArea, int maskBits );
int		trap_Phys_WorldContactEvents( int world, oaxPhysContact_t *out, int max );
/* bodies */
int		trap_Phys_BodyCreate( int world, const oaxPhysBodyDef_t *def );
void	trap_Phys_BodyDestroy( int body );
int		trap_Phys_BodyAddShape( int body, const oaxPhysShapeDef_t *def, const float *points, int numPoints, const int *indices, int numIndices );
void	trap_Phys_BodySetTransform( int body, const vec3_t origin, const float *quat );
void	trap_Phys_BodySetVelocity( int body, const float *velocity, const float *angularVelocity );
void	trap_Phys_BodyApply( int body, int kind, const vec3_t vec, const float *point );
void	trap_Phys_BodySetTarget( int body, const vec3_t origin, const float *quat, float seconds );
void	trap_Phys_BodySetParam( int body, int param, float value );
int		trap_Phys_BodyGetState( int body, oaxPhysBodyState_t *out );
int		trap_Phys_BodyGetStates( const int *bodies, int count, oaxPhysBodyState_t *out );
int		trap_Phys_BodyFromBSPModel( int world, int inlineModel, int type, const oaxPhysShapeDef_t *material );
float	trap_Phys_BodyGetMass( int body );
int		trap_Phys_RagdollCreate( int world, const oaxPhysRagdollDef_t *def, const oaxPhysRagdollBone_t *bones, int numBones, int *outBodies );
/* joints */
int		trap_Phys_JointCreate( int world, const oaxPhysJointDef_t *def );
void	trap_Phys_JointDestroy( int joint );
void	trap_Phys_JointSetParam( int joint, int param, float value );
float	trap_Phys_JointGetParam( int joint, int param );
/* queries */
int		trap_Phys_Raycast( int world, const oaxPhysRay_t *ray, oaxPhysHit_t *out );
int		trap_Phys_RaycastBatch( int world, const oaxPhysRay_t *rays, int count, oaxPhysHit_t *out );
int		trap_Phys_Shapecast( int world, const oaxPhysShapeDef_t *shape, const float *points, int numPoints,
			const oaxPhysRay_t *ray, const float *quat, oaxPhysHit_t *out );
int		trap_Phys_Overlap( int world, const oaxPhysShapeDef_t *shape, const float *points, int numPoints,
			const vec3_t origin, const float *quat, int maskBits, int *bodies, int max );

/* bg_oax_phys.c */
void	BG_PhysWorldDefInit( oaxPhysWorldDef_t *def, float gravity );
void	BG_PhysBodyDefInit( oaxPhysBodyDef_t *def, int type );
void	BG_PhysShapeDefInit( oaxPhysShapeDef_t *def, int type, float density );
void	BG_QuatFromAxis( vec3_t axis[3], float *quat );
void	BG_QuatToAxis( const float *quat, vec3_t axis[3] );
void	BG_QuatMul( const float *a, const float *b, float *out );
void	BG_QuatRotate( const float *q, const vec3_t v, vec3_t out );
void	BG_QuatFromAngles( const vec3_t angles, float *quat );

/* the stock ragdoll human: PHYS_HUMAN_BONES capsules for a player standing
   at `origin` (feet at origin[2]) facing `yaw` degrees; bone 0 is the pelvis */
#define PHYS_HUMAN_BONES 11
void	BG_PhysHumanBones( const vec3_t origin, float yaw, float scale, oaxPhysRagdollBone_t *bones );

#endif
