/*
===========================================================================
bg_oax_vehicle.h: vehicle types and the helpers the game and the cgame
share (bg_oax_vehicle.c). Phase 8: vehicles simulated on the server in the
engine's Box3D world (oa-engine code/physics/phys_vehicle.c, syscalls
1260-1269, token "physics_vehicle").

Body space: x forward, y left, z up, the origin at the chassis center
(the same axes as AnglesToAxis).
===========================================================================
*/
#ifndef BG_OAX_VEHICLE_H
#define BG_OAX_VEHICLE_H

#define OAX_VEH_BUGGY		0
#define OAX_VEH_HOVER		1
#define OAX_VEH_NUM_TYPES	2

#define OAX_VEH_MAX_WHEELS	4
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
} bgVehicleType_t;

extern const bgVehicleType_t bg_vehicleTypes[OAX_VEH_NUM_TYPES];

const bgVehicleType_t *BG_VehicleType( int type );
int		BG_VehicleTypeByName( const char *name );		/* -1 if unknown */

/* rotations: quaternions as bg_oax_phys.c (BG_QuatToAxis, BG_QuatFromAngles);
   the inverse of AnglesToAxis */
void	BG_VehAxisToAngles( vec3_t axis[3], vec3_t angles );
/* a body-space point to world space */
void	BG_VehLocalToWorld( const vec3_t origin, vec3_t axis[3], const vec3_t local, vec3_t out );

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
