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
bg_oax_vehicle.h: vehicle types and the helpers the game and the cgame
share (bg_oax_vehicle.c). Vehicles are simulated on the server in the
engine's Box3D world (syscalls 1260-1269, token "physics_vehicle"; see
code/physics/phys_vehicle.c in the oax engine,
github.com/monteslu/oax-engine).

Body space: x forward, y left, z up, the origin at the chassis center
(the same axes as AnglesToAxis).
===========================================================================
*/
#ifndef BG_OAX_VEHICLE_H
#define BG_OAX_VEHICLE_H

#define OAX_VEH_BUGGY		0
#define OAX_VEH_HOVER		1
#define OAX_VEH_NUM_TYPES	4

#define OAX_VEH_MAX_WHEELS	6
#define OAX_VEH_MAX_SEATS	2
#define OAX_VEH_SEAT_DRIVER	0
#define OAX_VEH_SEAT_GUNNER	1

/* playerState: in a vehicle (pm_flags bit 2 is free in OA) */
#define PMF_OAX_VEHICLE		4
/* playerState stats slot: vehicle entity number + 1 + seat * 1024, 0 on foot */
#define STAT_OAX_VEHICLE	14

typedef struct {
	const char	*name;				/* spawner "type" key */
	int			physType;			/* PHYS_VEHICLE_WHEELED / PHYS_VEHICLE_HOVER */
	float		halfExtents[3];
	float		mass;
	float		centerOfMass[3];
	int			numWheels;
	float		wheels[OAX_VEH_MAX_WHEELS][3];
	float		wheelRadius;
	float		restLength;
	float		springHertz;
	float		dampingRatio;
	float		engineForce;
	float		brakeForce;
	float		maxSpeed;
	float		maxReverse;
	float		maxSteer;
	float		steerSpeedFactor;
	float		steerRate;
	float		grip;
	float		lateralStiffness;
	float		handbrakeGrip;
	float		rollInfluence;
	float		turnTorque;
	float		bankAngle;
	int			numSeats;
	float		seats[OAX_VEH_MAX_SEATS][3];	/* occupant player origin, body space */
	int			health;
	const char	*model;
	const char	*wheelModel;
	/* what players collide with (g_oaxVehSolid): an oriented box in body
	   space, chassis and wheels together, so nobody walks through a wheel */
	float		colHalf[3];
	float		colOffset[3];
	int			mirrorLeftWheels;	/* one wheel model for both sides: turn the +y ones round */
	int			rearSteer;			/* the rear pair (wheels 2, 3) steers opposite the front:
									   a long six-wheeler turns about its middle axle */
	/* the mounted gun: fired by the occupant of gunSeat (-1 none), aimed
	   with that occupant's view angles within the limits below (degrees,
	   relative to the hull); the shot leaves the muzzle, gunMuzzle units
	   along the aim from the mount (the yaw pivot, body space) */
	int			gunSeat;
	int			gun;				/* OAX_VGUN_* */
	float		gunMount[3];
	float		gunMuzzle;
	float		gunYawArc;			/* 180: all round; else +- this from straight ahead */
	float		gunPitchUp, gunPitchDown;
	const char	*gunModel;			/* turns with the aim's yaw (and pitch, without a barrel model) */
	const char	*gunBarrelModel;	/* pitches about gunBarrelPivot (gun space); "" none */
	float		gunBarrelPivot[3];
	float		gunScale;
	float		gunModelOffset[3];	/* the gun model's origin in gun space (its barrels on the aim line) */
	/* the driver sits inside (hidden from everyone, out of reach of shots)
	   and sees out from the eye (body space; with the driver's gun, the eye
	   is in gun space and turns with the aim); the default view is this
	   first-person one rather than the chase camera */
	int			cockpit;
	float		eye[3];
	const char	*label;				/* the HUD's name for it */
} bgVehicleType_t;

/* mounted guns */
#define OAX_VGUN_NONE		0
#define OAX_VGUN_HMG		1	/* a heavy machine gun: hitscan, overheats */
#define OAX_VGUN_PLASMA		2	/* twin plasma bolts, from either side of the mount */
#define OAX_VGUN_CANNON		3	/* a heavy shell with splash damage, slow to reload */

typedef struct {
	int			intervalMs;			/* between shots */
	int			damage;
	float		spread;				/* hitscan: spread at 8192 units */
	float		heatPerShot;		/* 0: never overheats */
	float		coolPerSec;
	float		cooledAt;			/* an overheated gun fires again below this */
	const char	*fireSound;
	const char	*flashModel;
	float		flashColor[3];
} bgVehicleGun_t;

const bgVehicleGun_t *BG_VehicleGun( int gun );

/* the entity carries the gun's state: shots fired (powerups, wraps at
   65536) and heat (clientNum: 0-127, +128 while overheated) */
#define OAX_VEH_HEAT_LOCK	128

extern const bgVehicleType_t bg_vehicleTypes[OAX_VEH_NUM_TYPES];

const bgVehicleType_t *BG_VehicleType( int type );
int		BG_VehicleTypeByName( const char *name );		/* -1 if unknown */

/* rotations: quaternions as bg_oax_phys.c (BG_QuatToAxis, BG_QuatFromAngles);
   the inverse of AnglesToAxis */
void	BG_VehAxisToAngles( vec3_t axis[3], vec3_t angles );
/* a body-space point to world space */
void	BG_VehLocalToWorld( const vec3_t origin, vec3_t axis[3], const vec3_t local, vec3_t out );
/* the mounted gun's aim from its user's view angles, held to the type's
   limits: yaw and pitch relative to the hull (degrees; pitch up positive),
   the muzzle and the direction of the shot in the world; qfalse without a gun */
qboolean BG_VehGunAim( int type, const vec3_t origin, vec3_t axis[3], const vec3_t viewangles,
	float *yawRel, float *pitchRel, vec3_t muzzle, vec3_t dir );
/* the seat whose occupant fires the gun, -1 none */
int		BG_VehGunSeat( int type );

/* the rest needs the physics ABI (oax_phys.h, via bg_oax_phys.h) */
#ifdef OAX_PHYS_H
/* the physics vehicle of a type: the server's authoritative one and the
   cgame's predicted one are built by this one function */
void	BG_VehicleDef( int type, const vec3_t origin, const float *quat, oaxPhysVehicleDef_t *def );
/* a driver's command as vehicle controls (forward = throttle, strafe =
   steer, jump = handbrake, crouch = brake) */
void	BG_VehCmdToInput( const usercmd_t *cmd, oaxPhysVehicleInput_t *in );
/* the collision box for G_OAX_ENT_SET_OBB / CG_OAX_CM_TEMP_OBB: center
   offset from the vehicle origin, axis, half extents (15 floats) */
void	BG_VehOBB( int type, vec3_t axis[3], float *obb );

/* the vehicle state an ET_OAX_VEHICLE entity carries, exactly (floats are
   sent bit for bit), so a driving client can predict from it:
   pos.trBase origin, pos.trDelta velocity, pos.trTime the world time of
   the state, apos.trBase angles (drawing), apos.trDelta angular velocity,
   angles quat x y z, time quat w (bits), time2 steering radians (bits),
   legsAnim wheel contacts, torsoAnim OAX_VEHS_* flags */
#define OAX_VEHS_AWAKE		1
void	BG_VehStateToEntity( const oaxPhysVehicleState_t *st, int worldTime, int flags, entityState_t *s );
void	BG_VehStateFromEntity( const entityState_t *s, oaxPhysVehicleState_t *st, int *worldTime, int *flags );
#endif

/* vehicle physics ticks: 16 ms of level time, on absolute multiples of 16 */
#define OAX_VEH_TICK_MSEC	16

#endif
