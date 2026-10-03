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
bg_oax_phys_syscalls.h: C wrappers of the physics traps for DLL builds of
the game and the cgame. Included at the end of g_syscalls.c and
cg_syscalls.c (their `syscall` and PASSFLOAT); QVM builds use the equ
lines in g_syscalls.asm / cg_syscalls.asm instead.
===========================================================================
*/
#include "bg_oax_phys.h"

static float Phys_IntAsFloat( intptr_t i ) {
	floatint_t fi;
	fi.i = (int)i;
	return fi.f;
}

int trap_Phys_WorldCreate( const oaxPhysWorldDef_t *def ) { return syscall( PHYS_WORLD_CREATE, def ); }
void trap_Phys_WorldDestroy( int world ) { syscall( PHYS_WORLD_DESTROY, world ); }
int trap_Phys_WorldStep( int world, int msec ) { return syscall( PHYS_WORLD_STEP, world, msec ); }
int trap_Phys_WorldAddBSP( int world, int contentsMask, int flags, const oaxPhysShapeDef_t *material ) {
	return syscall( PHYS_WORLD_ADD_BSP, world, contentsMask, flags, material );
}
int trap_Phys_WorldAddHeightField( int world, const oaxPhysHeightField_t *hf, const float *heights, const oaxPhysShapeDef_t *material ) {
	return syscall( PHYS_WORLD_ADD_HEIGHTFIELD, world, hf, heights, material );
}
void trap_Phys_WorldSetGravity( int world, const vec3_t gravity ) { syscall( PHYS_WORLD_SET_GRAVITY, world, gravity ); }
int trap_Phys_WorldStats( int world, oaxPhysStats_t *out ) { return syscall( PHYS_WORLD_STATS, world, out ); }
int trap_Phys_WorldHash( int world ) { return syscall( PHYS_WORLD_HASH, world ); }
void trap_Phys_WorldExplode( int world, const vec3_t origin, float radius, float falloff, float impulsePerArea, int maskBits ) {
	syscall( PHYS_WORLD_EXPLODE, world, origin, PASSFLOAT( radius ), PASSFLOAT( falloff ), PASSFLOAT( impulsePerArea ), maskBits );
}
int trap_Phys_WorldContactEvents( int world, oaxPhysContact_t *out, int max ) { return syscall( PHYS_WORLD_CONTACT_EVENTS, world, out, max ); }
int trap_Phys_BodyCreate( int world, const oaxPhysBodyDef_t *def ) { return syscall( PHYS_BODY_CREATE, world, def ); }
void trap_Phys_BodyDestroy( int body ) { syscall( PHYS_BODY_DESTROY, body ); }
int trap_Phys_BodyAddShape( int body, const oaxPhysShapeDef_t *def, const float *points, int numPoints, const int *indices, int numIndices ) {
	return syscall( PHYS_BODY_ADD_SHAPE, body, def, points, numPoints, indices, numIndices );
}
void trap_Phys_BodySetTransform( int body, const vec3_t origin, const float *quat ) { syscall( PHYS_BODY_SET_TRANSFORM, body, origin, quat ); }
void trap_Phys_BodySetVelocity( int body, const float *velocity, const float *angularVelocity ) {
	syscall( PHYS_BODY_SET_VELOCITY, body, velocity, angularVelocity );
}
void trap_Phys_BodyApply( int body, int kind, const vec3_t vec, const float *point ) { syscall( PHYS_BODY_APPLY, body, kind, vec, point ); }
void trap_Phys_BodySetTarget( int body, const vec3_t origin, const float *quat, float seconds ) {
	syscall( PHYS_BODY_SET_TARGET, body, origin, quat, PASSFLOAT( seconds ) );
}
void trap_Phys_BodySetParam( int body, int param, float value ) { syscall( PHYS_BODY_SET_PARAM, body, param, PASSFLOAT( value ) ); }
int trap_Phys_BodyGetState( int body, oaxPhysBodyState_t *out ) { return syscall( PHYS_BODY_GET_STATE, body, out ); }
int trap_Phys_BodyGetStates( const int *bodies, int count, oaxPhysBodyState_t *out ) { return syscall( PHYS_BODY_GET_STATES, bodies, count, out ); }
int trap_Phys_BodyFromBSPModel( int world, int inlineModel, int type, const oaxPhysShapeDef_t *material ) {
	return syscall( PHYS_BODY_FROM_BSP_MODEL, world, inlineModel, type, material );
}
float trap_Phys_BodyGetMass( int body ) { return Phys_IntAsFloat( syscall( PHYS_BODY_GET_MASS, body ) ); }
int trap_Phys_RagdollCreate( int world, const oaxPhysRagdollDef_t *def, const oaxPhysRagdollBone_t *bones, int numBones, int *outBodies ) {
	return syscall( PHYS_RAGDOLL_CREATE, world, def, bones, numBones, outBodies );
}
int trap_Phys_JointCreate( int world, const oaxPhysJointDef_t *def ) { return syscall( PHYS_JOINT_CREATE, world, def ); }
void trap_Phys_JointDestroy( int joint ) { syscall( PHYS_JOINT_DESTROY, joint ); }
void trap_Phys_JointSetParam( int joint, int param, float value ) { syscall( PHYS_JOINT_SET_PARAM, joint, param, PASSFLOAT( value ) ); }
float trap_Phys_JointGetParam( int joint, int param ) { return Phys_IntAsFloat( syscall( PHYS_JOINT_GET_PARAM, joint, param ) ); }
int trap_Phys_Raycast( int world, const oaxPhysRay_t *ray, oaxPhysHit_t *out ) { return syscall( PHYS_RAYCAST, world, ray, out ); }
int trap_Phys_RaycastBatch( int world, const oaxPhysRay_t *rays, int count, oaxPhysHit_t *out ) {
	return syscall( PHYS_RAYCAST_BATCH, world, rays, count, out );
}
int trap_Phys_Shapecast( int world, const oaxPhysShapeDef_t *shape, const float *points, int numPoints,
		const oaxPhysRay_t *ray, const float *quat, oaxPhysHit_t *out ) {
	return syscall( PHYS_SHAPECAST, world, shape, points, numPoints, ray, quat, out );
}
int trap_Phys_Overlap( int world, const oaxPhysShapeDef_t *shape, const float *points, int numPoints,
		const vec3_t origin, const float *quat, int maskBits, int *bodies, int max ) {
	return syscall( PHYS_OVERLAP, world, shape, points, numPoints, origin, quat, maskBits, bodies, max );
}
