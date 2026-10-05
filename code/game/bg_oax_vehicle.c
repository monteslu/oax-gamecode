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
bg_oax_vehicle.c: the vehicle types (shared by the game, which builds the
physics vehicles from them, and the cgame, which draws the wheels and the
occupants from the same numbers) and rotation helpers.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_oax_phys.h"
#include "bg_oax_vehicle.h"

/* physics types, as in oax_phys.h (PHYS_VEHICLE_*) */
#define VEH_WHEELED	0
#define VEH_HOVER	1

const bgVehicleType_t bg_vehicleTypes[OAX_VEH_NUM_TYPES] = {
	{
		"buggy", VEH_WHEELED,
		{ 72, 40, 14 }, 900, { 0, 0, -12 },
		4, { { 52, 44, -6 }, { 52, -44, -6 }, { -52, 44, -6 }, { -52, -44, -6 } },
		20,		/* wheel radius */
		26,		/* suspension rest length */
		2.2f, 0.6f,		/* spring hertz, damping ratio */
		720000, 1500000,	/* engine, brakes */
		1100, 420,		/* max speed, reverse */
		0.55f, 0.6f, 2.5f,	/* lock, lock lost at speed, steer rate */
		1.4f, 0.5f, 0.35f, 0.6f,	/* grip, lateral stiffness, handbrake grip, roll influence */
		0, 0,
		2, { { 6, 16, 24 }, { -40, 0, 38 } },
		450,
		"models/oax/vehicles/buggy.md3", "models/oax/vehicles/wheel.md3",
		{ 72, 46, 30 }, { 0, 0, -16 },	/* deck at +14, down to the wheels' bottoms */
		0, 0,
		/* a heavy machine gun the gunner holds, its barrels a hand below the eye */
		OAX_VEH_SEAT_GUNNER, OAX_VGUN_HMG, { -34, 0, 50 }, 52, 180, 50, 25,
		"models/weapons/vulcan/vulcan.md3", "", { 0, 0, 0 }, 1.6f, { 0, 0, -8.6f },
		0, { 0, 0, 0 },
		"BUGGY"
	},
	{
		"hover", VEH_HOVER,
		{ 64, 36, 10 }, 500, { 0, 0, -6 },
		4, { { 48, 30, -10 }, { 48, -30, -10 }, { -48, 30, -10 }, { -48, -30, -10 } },
		0,
		36,		/* hover height */
		2.5f, 0.8f,
		330000, 600000,
		1300, 400,
		1.8f, 0, 0,		/* turn rate rad/s */
		0, 0.06f, 0, 0,
		6.0f, 0.35f,	/* yaw response, bank */
		1, { { -8, 0, 34 }, { 0, 0, 0 } },
		300,
		"models/oax/vehicles/hover.md3", "",
		{ 64, 36, 10 }, { 0, 0, 0 },
		0, 0,
		/* twin plasma guns in the nose, aimed a little either way */
		OAX_VEH_SEAT_DRIVER, OAX_VGUN_PLASMA, { 56, 0, 0 }, 16, 15, 15, 15,
		"", "", { 0, 0, 0 }, 1, { 0, 0, 0 },
		0, { 0, 0, 0 },
		"HOVER"
	},
	{
		/* a six-wheeled armoured carrier: slower, heavier, driver and a
		   gunner on the roof; model and wheel from models/oax/vehicles */
		"apc", VEH_WHEELED,
		{ 112, 58, 34 }, 2400, { 0, 0, -18 },
		/* front, rear, then the middle pair (not networked: drawn between) */
		6, { { 74, 47, -8 }, { 74, -47, -8 }, { -72, 47, -8 }, { -72, -47, -8 }, { 1, 47, -8 }, { 1, -47, -8 } },
		24,		/* wheel radius */
		20,		/* suspension rest length */
		1.8f, 0.7f,
		2000000, 4200000,
		850, 360,
		0.5f, 0.4f, 3.0f,	/* lock, lock lost at speed, steer rate (with rearSteer: twice the turn) */
		1.5f, 0.55f, 0.4f, 0.4f,
		0, 0,
		2, { { 40, 18, 30 }, { -20, 0, 70 } },
		900,
		"models/oax/vehicles/apc.md3", "models/oax/vehicles/apc_wheel.md3",
		{ 115, 63, 44 }, { 0, 0, -8 },	/* hull top at +36, down to the wheels' bottoms */
		1, 1,
		/* a heavy machine gun on the roof, held by the gunner as on the buggy */
		OAX_VEH_SEAT_GUNNER, OAX_VGUN_HMG, { -14, 0, 82 }, 52, 180, 50, 25,
		"models/weapons/vulcan/vulcan.md3", "", { 0, 0, 0 }, 1.6f, { 0, 0, -8.6f },
		/* the driver behind the armour, looking out over the bow */
		1, { 10, 0, 38 },
		"APC"
	},
	{
		/* an armoured hover tank: the turret is part of the model */
		"hovertank", VEH_HOVER,
		{ 78, 43, 11 }, 900, { 0, 0, -4 },
		4, { { 60, 34, -11 }, { 60, -34, -11 }, { -60, 34, -11 }, { -60, -34, -11 } },
		0,
		34,		/* hover height */
		2.2f, 0.85f,
		520000, 900000,
		1000, 360,
		1.4f, 0, 0,
		0, 0.06f, 0, 0,
		5.0f, 0.25f,
		1, { { -10, 0, 34 }, { 0, 0, 0 } },
		600,
		"models/oax/vehicles/hovertank.md3", "",
		{ 78, 43, 18 }, { 0, 0, 6 },
		0, 0,
		/* the turret's cannon: the turret turns about its centre, the barrel
		   pitches at the turret's front (models/oax/vehicles: the source
		   model's turret and barrel split out, each origin at its pivot) */
		OAX_VEH_SEAT_DRIVER, OAX_VGUN_CANNON, { -35, 0, 18 }, 104, 180, 25, 8,
		"models/oax/vehicles/hovertank_turret.md3", "models/oax/vehicles/hovertank_barrel.md3", { 16, 0, 0 }, 1, { 0, 0, 0 },
		/* the driver inside, sighting over the turret roof */
		1, { -6, 0, 13 },
		"HOVERTANK"
	}
};

static const bgVehicleGun_t bg_vehicleGuns[] = {
	{ 0 },
	/* OAX_VGUN_HMG */
	{ 75, 12, 260, 0.03f, 0.45f, 0.35f, "sound/weapons/vulcan/vulcanf1b.wav",
		"models/weapons/vulcan/vulcan_flash.md3", { 1, 0.75f, 0.3f } },
	/* OAX_VGUN_PLASMA */
	{ 110, 20, 0, 0, 0, 0, "sound/weapons/plasma/hyprbf1a.wav",
		"models/weapons2/plasma/plasma_flash.md3", { 0.6f, 0.6f, 1 } },
	/* OAX_VGUN_CANNON */
	{ 1100, 120, 0, 0, 0, 0, "sound/weapons/rocket/rocklf1a.wav",
		"models/weapons2/rocketl/rocketl_flash.md3", { 1, 0.7f, 0.3f } },
};

const bgVehicleGun_t *BG_VehicleGun( int gun ) {
	if ( gun < 0 || gun >= (int)( sizeof( bg_vehicleGuns ) / sizeof( bg_vehicleGuns[0] ) ) ) {
		return &bg_vehicleGuns[0];
	}
	return &bg_vehicleGuns[gun];
}

int BG_VehGunSeat( int type ) {
	const bgVehicleType_t *t = BG_VehicleType( type );
	return t->gun != OAX_VGUN_NONE ? t->gunSeat : -1;
}

qboolean BG_VehGunAim( int type, const vec3_t origin, vec3_t axis[3], const vec3_t viewangles,
	float *yawRel, float *pitchRel, vec3_t muzzle, vec3_t dir ) {
	const bgVehicleType_t *t = BG_VehicleType( type );
	vec3_t fwd, mount;
	float lx, ly, lz, yaw, pitch, cy, sy, cp, sp;
	int i;

	if ( t->gun == OAX_VGUN_NONE ) {
		return qfalse;
	}
	/* the view direction in body space */
	AngleVectors( viewangles, fwd, NULL, NULL );
	lx = DotProduct( fwd, axis[0] );
	ly = DotProduct( fwd, axis[1] );
	lz = DotProduct( fwd, axis[2] );
	yaw = atan2( ly, lx ) * ( 180.0f / M_PI );
	pitch = atan2( lz, sqrt( lx * lx + ly * ly ) ) * ( 180.0f / M_PI );
	if ( yaw > t->gunYawArc ) {
		yaw = t->gunYawArc;
	} else if ( yaw < -t->gunYawArc ) {
		yaw = -t->gunYawArc;
	}
	if ( pitch > t->gunPitchUp ) {
		pitch = t->gunPitchUp;
	} else if ( pitch < -t->gunPitchDown ) {
		pitch = -t->gunPitchDown;
	}
	cy = cos( DEG2RAD( yaw ) );
	sy = sin( DEG2RAD( yaw ) );
	cp = cos( DEG2RAD( pitch ) );
	sp = sin( DEG2RAD( pitch ) );
	for ( i = 0; i < 3; i++ ) {
		dir[i] = cp * cy * axis[0][i] + cp * sy * axis[1][i] + sp * axis[2][i];
	}
	BG_VehLocalToWorld( origin, axis, t->gunMount, mount );
	VectorMA( mount, t->gunMuzzle, dir, muzzle );
	if ( yawRel ) {
		*yawRel = yaw;
	}
	if ( pitchRel ) {
		*pitchRel = pitch;
	}
	return qtrue;
}

const bgVehicleType_t *BG_VehicleType( int type ) {
	if ( type < 0 || type >= OAX_VEH_NUM_TYPES ) {
		return &bg_vehicleTypes[0];
	}
	return &bg_vehicleTypes[type];
}

int BG_VehicleTypeByName( const char *name ) {
	int i;

	for ( i = 0; i < OAX_VEH_NUM_TYPES; i++ ) {
		if ( !Q_stricmp( name, bg_vehicleTypes[i].name ) ) {
			return i;
		}
	}
	return -1;
}

/* the inverse of AnglesToAxis: forward = axis[0], left = axis[1], up = axis[2] */
void BG_VehAxisToAngles( vec3_t axis[3], vec3_t angles ) {
	float fxy = sqrt( axis[0][0] * axis[0][0] + axis[0][1] * axis[0][1] );

	angles[PITCH] = -atan2( axis[0][2], fxy ) * ( 180.0f / M_PI );
	if ( fxy > 0.0001f ) {
		angles[YAW] = atan2( axis[0][1], axis[0][0] ) * ( 180.0f / M_PI );
		angles[ROLL] = atan2( axis[1][2], axis[2][2] ) * ( 180.0f / M_PI );
	} else {
		/* pointing straight up or down: put it all in yaw */
		angles[YAW] = atan2( -axis[1][0], axis[1][1] ) * ( 180.0f / M_PI );
		angles[ROLL] = 0;
	}
}

void BG_VehLocalToWorld( const vec3_t origin, vec3_t axis[3], const vec3_t local, vec3_t out ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		out[i] = origin[i] + local[0] * axis[0][i] + local[1] * axis[1][i] + local[2] * axis[2][i];
	}
}

void BG_VehicleDef( int type, const vec3_t origin, const float *quat, oaxPhysVehicleDef_t *def ) {
	const bgVehicleType_t *t = BG_VehicleType( type );
	int i;

	memset( def, 0, sizeof( *def ) );
	def->type = t->physType;
	VectorCopy( origin, def->origin );
	for ( i = 0; i < 4; i++ ) {
		def->quat[i] = quat[i];
	}
	VectorCopy( t->halfExtents, def->halfExtents );
	def->mass = t->mass;
	VectorCopy( t->centerOfMass, def->centerOfMass );
	def->linearDamping = 0.05f;
	def->angularDamping = 0.6f;
	def->friction = 0.4f;
	def->restitution = 0.1f;
	def->numWheels = t->numWheels;
	for ( i = 0; i < t->numWheels; i++ ) {
		VectorCopy( t->wheels[i], def->wheels[i] );
	}
	def->driveMask = ( 1 << t->numWheels ) - 1;
	def->steerMask = t->rearSteer ? 15 : 3;	/* the front pair; rear too (steering opposite, x < 0) */
	def->wheelRadius = t->wheelRadius;
	def->restLength = t->restLength;
	def->springHertz = t->springHertz;
	def->dampingRatio = t->dampingRatio;
	def->engineForce = t->engineForce;
	def->brakeForce = t->brakeForce;
	def->maxSpeed = t->maxSpeed;
	def->maxReverse = t->maxReverse;
	def->maxSteer = t->maxSteer;
	def->steerSpeedFactor = t->steerSpeedFactor;
	def->steerRate = t->steerRate;
	def->grip = t->grip;
	def->lateralStiffness = t->lateralStiffness;
	def->handbrakeGrip = t->handbrakeGrip;
	def->rollInfluence = t->rollInfluence;
	def->turnTorque = t->turnTorque;
	def->bankAngle = t->bankAngle;
	def->flags = PHYS_VF_AUTOFLIP;
}

void BG_VehCmdToInput( const usercmd_t *cmd, oaxPhysVehicleInput_t *in ) {
	in->throttle = cmd->forwardmove / 127.0f;
	in->steer = -cmd->rightmove / 127.0f;
	in->handbrake = cmd->upmove > 0 ? 1.0f : 0.0f;
	in->brake = cmd->upmove < 0 ? 1.0f : 0.0f;
}

void BG_VehOBB( int type, vec3_t axis[3], float *obb ) {
	const bgVehicleType_t *t = BG_VehicleType( type );
	int i;

	for ( i = 0; i < 3; i++ ) {
		obb[i] = t->colOffset[0] * axis[0][i] + t->colOffset[1] * axis[1][i] + t->colOffset[2] * axis[2][i];
		obb[3 + i] = axis[0][i];
		obb[6 + i] = axis[1][i];
		obb[9 + i] = axis[2][i];
		obb[12 + i] = t->colHalf[i];
	}
}

void BG_VehStateToEntity( const oaxPhysVehicleState_t *st, int worldTime, int flags, entityState_t *s ) {
	floatint_t fi;

	VectorCopy( st->origin, s->pos.trBase );
	VectorCopy( st->velocity, s->pos.trDelta );
	s->pos.trTime = worldTime;
	VectorCopy( st->angularVelocity, s->apos.trDelta );
	s->angles[0] = st->quat[0];
	s->angles[1] = st->quat[1];
	s->angles[2] = st->quat[2];
	fi.f = st->quat[3];
	s->time = fi.i;
	fi.f = st->steer;
	s->time2 = fi.i;
	s->legsAnim = st->contacts & 255;
	s->torsoAnim = flags & 255;
}

void BG_VehStateFromEntity( const entityState_t *s, oaxPhysVehicleState_t *st, int *worldTime, int *flags ) {
	floatint_t fi;

	memset( st, 0, sizeof( *st ) );
	VectorCopy( s->pos.trBase, st->origin );
	VectorCopy( s->pos.trDelta, st->velocity );
	VectorCopy( s->apos.trDelta, st->angularVelocity );
	st->quat[0] = s->angles[0];
	st->quat[1] = s->angles[1];
	st->quat[2] = s->angles[2];
	fi.i = s->time;
	st->quat[3] = fi.f;
	fi.i = s->time2;
	st->steer = fi.f;
	st->contacts = s->legsAnim;
	/* the wheels as drawn: suspension in origin2 and angles2[0] */
	st->numWheels = BG_VehicleType( s->generic1 )->numWheels;
	st->suspension[0] = s->origin2[0];
	st->suspension[1] = s->origin2[1];
	st->suspension[2] = s->origin2[2];
	st->suspension[3] = s->angles2[0];
	/* six wheels: the middle pair is not sent; drawn between front and back */
	st->suspension[4] = 0.5f * ( st->suspension[0] + st->suspension[2] );
	st->suspension[5] = 0.5f * ( st->suspension[1] + st->suspension[3] );
	if ( worldTime ) {
		*worldTime = s->pos.trTime;
	}
	if ( flags ) {
		*flags = s->torsoAnim;
	}
}
