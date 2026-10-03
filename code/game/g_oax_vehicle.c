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
g_oax_vehicle.c: vehicles on the server.

The game owns one authoritative Box3D world (the engine's physics module)
with the map's brushes and heightmap terrain as static collision. Each
vehicle is a physics vehicle there (oax_phys.h: a box chassis with
raycast wheels, or a hover craft on thruster rays) plus an ET_OAX_VEHICLE
entity the clients see. The entity carries the vehicle's full physics
state bit for bit (BG_VehStateToEntity) and the world time of it: other
clients draw it interpolated between snapshots, and its driver predicts it
from that state with the same physics (cg_oax_vehicle.c).

Time: the world runs fixed 16 ms ticks of its own clock (vehWorldTime, on
absolute multiples of 16 ms of level time), one tick at a time
(G_VehAdvance), so a driver's commands are applied by their command time
with no delay: the tick that starts at T drives with the last command at
or before T. The clock only runs while every input for the next tick is
known: never past the command time of any human player in the game (their
commands arrive whenever the network delivers them, always in order), and
never past the next server frame (whose events, bot commands and
explosions, are stamped with its level time). Humans move the clock from
their own ClientThink before their pmove, so a player sees the vehicles
exactly as of its command time; the server frame moves it for everyone
else. Whatever order packets and frames arrive in, every tick sees the
same inputs: native and the cart give the same vehicles (vehicle-parity),
with prediction on or off.

- Rule: g_oaxVehicles 1 (server info, latched). With it off, every
  info_oax_vehicle spawner frees itself and nothing here runs: classic
  play is untouched.
- Spawners: info_oax_vehicle, keys "type" (buggy, hover), "angle", "wait"
  (respawn seconds after the vehicle is destroyed, default 20).
- Seats: the use button (+button2) gets in (driver first, then gunner) and
  out. A driver steers with the movement keys or the left stick (forward
  = throttle, strafe = steer, jump = handbrake, crouch = brake); the
  gunner stands on the rear deck and can shoot. Occupants carry the
  PMF_OAX_VEHICLE flag (pmove does not move them, the gunner's weapon
  works) and STAT_OAX_VEHICLE; their origin is the seat, rewritten after
  every physics step.
- Solid (g_oaxVehSolid, a cheat cvar, 1): players collide with the
  vehicle's oriented box (BG_VehOBB through the engine's "ent_obb"), on
  the server and in the cgame's prediction alike: they cannot walk or fall
  through it, stand on its deck and ride it (carried tick by tick), and a
  moving vehicle pushes them aside (or runs them over, the roadkill rule).
- Explosions and hits push vehicles (G_OAXVehicleImpulse from G_Damage):
  an impulse at the hit, applied at the first tick at or after its level
  time, deterministic like everything else here.
- Damage: vehicles take weapon damage; at zero health the occupants are
  thrown out and the vehicle explodes (radius damage), and its spawner
  builds a new one after "wait" seconds. Empty vehicles left away from
  their spawner (or on their roof) for 30 s go back to it the same way.
  Hard hits (over 600 u/s, into the world or another vehicle) hurt the
  vehicle; a fast vehicle hurts players it hits.
- Debug values (tests): g_veh_* (counts, enters, distance driven, flips,
  vehicles in solid, physics ticks and step time, a state hash).

Determinism: every input is the game state; the physics world steps the
level time in fixed 16 ms ticks inside the engine, identical on every
build (vehicle-parity).
===========================================================================
*/
#include "g_local.h"
#include "oax_public.h"
#include "g_oax_vehicle.h"

#define VEH_MAX				32
#define VEH_ABANDON_MS		30000
#define VEH_ROADKILL_SPEED	300.0f
#define VEH_KILL_Z			-4000.0f
#define VEH_INQ				32		/* queued driver commands per vehicle */
#define VEH_CRASH_SPEED		600.0f	/* hits faster than this hurt the vehicle */
#define VEH_TICK			OAX_VEH_TICK_MSEC
#define VEH_MAX_LAG			300		/* a human this far behind no longer holds the clock back */
#define VEH_MAX_IMPULSES	64
#define VEH_KNOCKBACK_SCALE	4.0f	/* impulse (kg u/s) per knockback point per g_knockback */

int		G_PhysWorld( void );		/* g_oax_phys.c: the level's authoritative world */
void	trap_OAX_EntSetOBB( int entnum, const float *obb );	/* g_syscalls.asm */

typedef struct {
	int			inuse;
	gentity_t	*ent;
	gentity_t	*spawner;
	int			type;
	int			handle;			/* physics vehicle */
	int			occupant[OAX_VEH_MAX_SEATS];	/* client number, -1 */
	oaxPhysVehicleInput_t input;	/* what the physics vehicle drives with now */
	/* the driver's controls by command time: the tick starting at T takes
	   the last one at or before T */
	int			qTime[VEH_INQ];
	oaxPhysVehicleInput_t q[VEH_INQ];
	int			qCount;
	oaxPhysVehicleState_t state;
	vec3_t		axis[3];
	float		lastDistance;
	int			emptySince;
	int			upsideSince;
	int			dying;			/* destroyed this frame: explode in the vehicle frame */
	int			attacker;		/* entity number credited with the kill */
	int			driveStart;		/* level time a driver first got in, 0 before */
	int			nextCheckpoint;	/* drive-relative time of the next g_veh_drive_t value */
	int			awake;			/* the chassis moved last tick (the cgame wakes its copy) */
	vec3_t		prevOrigin;		/* before the tick, for carrying riders */
	vec3_t		prevAxis[3];
} gVehicle_t;

typedef struct {
	int			time;			/* level time of the hit */
	int			ent;			/* vehicle entity number */
	vec3_t		impulse;
	vec3_t		point;
} gVehImpulse_t;

typedef struct {
	int			veh;			/* index into gVehicles, -1 on foot */
	int			seat;
	int			useHeld;
	int			useEaten;		/* use got someone in or out: hidden from the game until released */
	int			lastRoadkill;
} gVehClient_t;

static gVehicle_t	gVehicles[VEH_MAX];
static gVehClient_t	gVehClients[MAX_CLIENTS];
static int			gVehWorld;
static int			gVehSpawners;
static vmCvar_t		g_oaxVehicles;
static vmCvar_t		g_oaxVehLog;	/* 1: one console line per driven vehicle per frame (debugging) */
static vmCvar_t		g_oaxVehSolid;	/* cheat: 0 = players pass through vehicles (test control) */
static int			vehTerrainBodies;
static int			vehWorldTime;	/* level time the world's state is at (a tick boundary) */
static int			vehCmdTime[MAX_CLIENTS];	/* last command time of each human, 0 = none yet */
static gVehImpulse_t	vehImpulses[VEH_MAX_IMPULSES];
static int			vehNumImpulses;

/* statistics for the tests */
static int		vehLateCmds, vehCmdLagMax, vehCrashes, vehGunnerShots;
static int		vehAheadCmds, vehHeldTicks, vehImpulseCount, vehCarried, vehPushed, vehBlockedPush;
static float	vehImpulseTotal;
static int		vehGunAmmo[MAX_CLIENTS];	/* a gunner's ammo last frame, for counting shots */
static int		vehSpawned, vehDestroyed, vehResets, vehEnters, vehBotEnters, vehExits, vehRoadkills;
static int		vehInSolid, vehFell, vehFrames, vehDrivenFrames, vehBotDrivenFrames, vehGunnerFrames;
static float	vehDistance, vehBotDistance, vehMinZ;
static unsigned	vehHash;
static unsigned	vehDriveHash;
static int		vehCheckpointVeh;	/* the vehicle slot the drive checkpoints follow, -1 */	/* every driven vehicle's state, times relative to its first driver */
static int		vehNextHashTime;

static void G_VehAdvance( int cap );
static int G_VehFrameMsec( void );

int G_OAXVehiclesActive( void ) {
	return gVehWorld != 0;
}

/* ---- the physics world -------------------------------------------------------- */

/* the level's authoritative world (g_oax_phys.c: brushes and patches),
   plus the heightmap terrain the first time vehicles use it */
static int G_VehWorld( void ) {
	oaxPhysShapeDef_t mat;
	oaxPhysStats_t st;
	int bodies[8];

	if ( gVehWorld ) {
		return gVehWorld;
	}
	gVehWorld = G_PhysWorld();
	if ( !gVehWorld ) {
		G_Printf( S_COLOR_YELLOW "vehicles: no physics world\n" );
		return 0;
	}
	/* ticks on absolute multiples of 16 ms of level time, so the tick
	   schedule never depends on when the map started */
	memset( &st, 0, sizeof( st ) );
	trap_Phys_WorldStats( gVehWorld, &st );
	vehWorldTime = level.time - level.time % VEH_TICK;
	BG_PhysShapeDefInit( &mat, PHYS_SHAPE_BOX, 0 );
	mat.friction = 0.8f;
	/* g_oaxVehNoTerrain 1 is a test control: vehicles fall through the terrain */
	vehTerrainBodies = trap_Cvar_VariableIntegerValue( "g_oaxVehNoTerrain" ) ? 0 :
		trap_Phys_WorldAddTerrain( gVehWorld, &mat, bodies, 8 );
	return gVehWorld;
}

/* ---- lookups -------------------------------------------------------------------- */

static gVehicle_t *G_VehOfEnt( gentity_t *ent ) {
	int i;

	if ( !ent ) {
		return NULL;
	}
	for ( i = 0; i < VEH_MAX; i++ ) {
		if ( gVehicles[i].inuse && gVehicles[i].ent == ent ) {
			return &gVehicles[i];
		}
	}
	return NULL;
}

int G_OAXVehicleOfClient( int clientNum, int *seat ) {
	gVehClient_t *vc;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return -1;
	}
	vc = &gVehClients[clientNum];
	if ( vc->veh < 0 || !gVehicles[vc->veh].inuse ) {
		return -1;
	}
	if ( seat ) {
		*seat = vc->seat;
	}
	return gVehicles[vc->veh].ent->s.number;
}

int G_OAXVehicleCount( void ) {
	int i, n = 0;

	for ( i = 0; i < VEH_MAX; i++ ) {
		if ( gVehicles[i].inuse && !gVehicles[i].dying ) {
			n++;
		}
	}
	return n;
}

gentity_t *G_OAXVehicleEnt( int k ) {
	int i;

	for ( i = 0; i < VEH_MAX; i++ ) {
		if ( gVehicles[i].inuse && !gVehicles[i].dying ) {
			if ( k == 0 ) {
				return gVehicles[i].ent;
			}
			k--;
		}
	}
	return NULL;
}

int G_OAXVehicleSeatFree( gentity_t *veh, int seat ) {
	gVehicle_t *v = G_VehOfEnt( veh );

	if ( !v || v->dying || seat < 0 || seat >= BG_VehicleType( v->type )->numSeats ) {
		return 0;
	}
	return v->occupant[seat] < 0;
}

const oaxPhysVehicleState_t *G_OAXVehicleState( gentity_t *veh ) {
	gVehicle_t *v = G_VehOfEnt( veh );
	return v ? &v->state : NULL;
}

int G_OAXVehicleType( gentity_t *veh ) {
	gVehicle_t *v = G_VehOfEnt( veh );
	return v ? v->type : -1;
}

float G_OAXVehicleReach( gentity_t *veh ) {
	gVehicle_t *v = G_VehOfEnt( veh );
	const bgVehicleType_t *t;

	if ( !v ) {
		return 0;
	}
	t = BG_VehicleType( v->type );
	return ( t->halfExtents[0] > t->halfExtents[1] ? t->halfExtents[0] : t->halfExtents[1] ) + 64.0f;
}

/* ---- entity state ---------------------------------------------------------------- */

/* the vehicle entity from the physics state: position, angles, the full
   state for prediction, a world box around the rotated chassis, wheels and
   occupants for the cgame, and the oriented box players collide with */
static void G_VehUpdateEntity( gVehicle_t *v ) {
	gentity_t *ent = v->ent;
	const bgVehicleType_t *t = BG_VehicleType( v->type );
	vec3_t angles;
	float obb[15];
	int i, j, solid;

	BG_QuatToAxis( v->state.quat, v->axis );
	BG_VehAxisToAngles( v->axis, angles );

	ent->s.pos.trType = TR_INTERPOLATE;
	ent->s.apos.trType = TR_INTERPOLATE;
	VectorCopy( angles, ent->s.apos.trBase );
	BG_VehStateToEntity( &v->state, vehWorldTime, v->awake ? OAX_VEHS_AWAKE : 0, &ent->s );
	VectorCopy( v->state.origin, ent->r.currentOrigin );
	VectorCopy( angles, ent->r.currentAngles );
	VectorCopy( v->state.origin, ent->s.origin );

	/* wheels: suspension lengths in origin2[0..2] and angles2[0], steering
	   (degrees) in angles2[1], the first wheel's roll (degrees) in angles2[2] */
	for ( i = 0; i < 3; i++ ) {
		ent->s.origin2[i] = v->state.suspension[i];
	}
	ent->s.angles2[0] = v->state.suspension[3];
	ent->s.angles2[1] = RAD2DEG( v->state.steer );
	ent->s.angles2[2] = AngleMod( RAD2DEG( v->state.spin[0] ) );
	ent->s.otherEntityNum = v->occupant[OAX_VEH_SEAT_DRIVER] >= 0 ? v->occupant[OAX_VEH_SEAT_DRIVER] : ENTITYNUM_NONE;
	ent->s.otherEntityNum2 = v->occupant[OAX_VEH_SEAT_GUNNER] >= 0 ? v->occupant[OAX_VEH_SEAT_GUNNER] : ENTITYNUM_NONE;
	ent->s.frame = ent->health > 0 ? ent->health * 100 / t->health : 0;
	/* the gunner's own shots pass through the vehicle under them */
	ent->r.ownerNum = v->occupant[OAX_VEH_SEAT_GUNNER] >= 0 ? v->occupant[OAX_VEH_SEAT_GUNNER] : ENTITYNUM_NONE;

	/* world box of the rotated chassis, wheels included, and of the
	   collision box */
	BG_VehOBB( v->type, v->axis, obb );
	for ( i = 0; i < 3; i++ ) {
		float e = 0, c = 0;
		for ( j = 0; j < 3; j++ ) {
			float h = t->halfExtents[j];
			if ( j == 2 ) {
				h += t->physType == 1 ? 0 : t->restLength * 0.5f;
			}
			e += fabs( v->axis[j][i] ) * h;
			c += fabs( v->axis[j][i] ) * t->colHalf[j];
		}
		ent->r.mins[i] = -e;
		ent->r.maxs[i] = e;
		if ( obb[i] - c < ent->r.mins[i] ) {
			ent->r.mins[i] = obb[i] - c;
		}
		if ( obb[i] + c > ent->r.maxs[i] ) {
			ent->r.maxs[i] = obb[i] + c;
		}
	}
	/* players collide with it (g_oaxVehSolid), shots hit it either way */
	solid = g_oaxVehSolid.integer && BG_OAXFeature( "ent_obb" );
	ent->r.contents = solid ? CONTENTS_BODY : CONTENTS_CORPSE;
	if ( solid ) {
		trap_OAX_EntSetOBB( ent->s.number, obb );
	} else if ( BG_OAXFeature( "ent_obb" ) ) {
		trap_OAX_EntSetOBB( ent->s.number, NULL );
	}
	trap_LinkEntity( ent );
}

/* seat position in the world */
static void G_VehSeatOrigin( gVehicle_t *v, int seat, vec3_t out ) {
	BG_VehLocalToWorld( v->state.origin, v->axis, BG_VehicleType( v->type )->seats[seat], out );
}

/* put an occupant on its seat (after every physics step and in ClientThink) */
static void G_VehPlaceOccupant( gVehicle_t *v, int seat, gentity_t *ent ) {
	gclient_t *cl = ent->client;
	vec3_t org;

	G_VehSeatOrigin( v, seat, org );
	VectorCopy( org, cl->ps.origin );
	VectorCopy( v->state.velocity, cl->ps.velocity );
	cl->ps.pm_flags |= PMF_OAX_VEHICLE;
	cl->ps.stats[STAT_OAX_VEHICLE] = v->ent->s.number + 1 + seat * 1024;
	cl->ps.groundEntityNum = ENTITYNUM_NONE;
	BG_PlayerStateToEntityState( &cl->ps, &ent->s, qtrue );
	VectorCopy( cl->ps.origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

/* ---- controls ---------------------------------------------------------------------- */

static void G_VehParkedInput( oaxPhysVehicleInput_t *in ) {
	memset( in, 0, sizeof( *in ) );
	in->brake = 1.0f;
	in->handbrake = 1.0f;
}

/* the controls from time on (a driver's command time, or a seat change) */
static void G_VehQueueInput( gVehicle_t *v, int time, const oaxPhysVehicleInput_t *in ) {
	int i;

	if ( level.time - time > vehCmdLagMax ) {
		vehCmdLagMax = level.time - time;
	}
	/* a tick that should have used it already ran */
	if ( time <= vehWorldTime - VEH_TICK ) {
		vehLateCmds++;
	}
	if ( v->qCount > 0 && time <= v->qTime[v->qCount - 1] ) {
		time = v->qTime[v->qCount - 1];
		v->q[v->qCount - 1] = *in;
		return;
	}
	if ( v->qCount == VEH_INQ ) {
		for ( i = 1; i < VEH_INQ; i++ ) {
			v->qTime[i - 1] = v->qTime[i];
			v->q[i - 1] = v->q[i];
		}
		v->qCount--;
	}
	v->qTime[v->qCount] = time;
	v->q[v->qCount] = *in;
	v->qCount++;
}

/* the controls for the tick that starts at target: the last queued at or before it */
static void G_VehTakeInput( gVehicle_t *v, int target ) {
	int i, k = -1;

	for ( i = 0; i < v->qCount && v->qTime[i] <= target; i++ ) {
		k = i;
	}
	if ( k < 0 ) {
		return;
	}
	v->input = v->q[k];
	for ( i = k + 1; i < v->qCount; i++ ) {
		v->qTime[i - k - 1] = v->qTime[i];
		v->q[i - k - 1] = v->q[i];
	}
	v->qCount -= k + 1;
}

/* ---- seats ------------------------------------------------------------------------- */

static void G_VehEnter( gVehicle_t *v, int seat, gentity_t *ent, int time ) {
	int cn = ent->s.number;

	v->occupant[seat] = cn;
	gVehClients[cn].veh = (int)( v - gVehicles );
	gVehClients[cn].seat = seat;
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	/* the driver sits inside the chassis (shots hit the vehicle), the
	   gunner stands exposed on the deck */
	ent->r.contents = seat == OAX_VEH_SEAT_DRIVER ? 0 : CONTENTS_BODY;
	if ( ent->client->hook ) {
		Weapon_HookFree( ent->client->hook );
	}
	vehEnters++;
	vehGunAmmo[cn] = ent->client->ps.ammo[ent->client->ps.weapon];
	if ( seat == OAX_VEH_SEAT_DRIVER && !v->driveStart ) {
		v->driveStart = time;
		v->nextCheckpoint = 0;
	}
	if ( ent->r.svFlags & SVF_BOT ) {
		vehBotEnters++;
	}
	v->emptySince = 0;
	G_VehPlaceOccupant( v, seat, ent );
}

/* take a client off its seat; place: find room beside the vehicle */
static qboolean G_VehLeave( int cn, qboolean place, int time ) {
	gVehClient_t *vc = &gVehClients[cn];
	gentity_t *ent = &g_entities[cn];
	gVehicle_t *v;
	static const float spots[][3] = {
		{ 0, 1, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { -1, 1, 0 }, { -1, -1, 0 }, { 0, 0, 1 }
	};
	vec3_t mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	int i;

	if ( vc->veh < 0 ) {
		return qtrue;
	}
	v = &gVehicles[vc->veh];
	if ( ent->inuse && ent->client && place && ent->health > 0 ) {
		const bgVehicleType_t *t = BG_VehicleType( v->type );
		qboolean ok = qfalse;
		for ( i = 0; i < (int)( sizeof( spots ) / sizeof( spots[0] ) ) && !ok; i++ ) {
			vec3_t local, pos, from;
			trace_t tr;
			local[0] = spots[i][0] * ( t->halfExtents[0] + 40 );
			local[1] = spots[i][1] * ( t->halfExtents[1] + 40 );
			local[2] = spots[i][2] * ( t->halfExtents[2] + 40 ) + 8;
			BG_VehLocalToWorld( v->state.origin, v->axis, local, pos );
			/* from above the vehicle center, so nothing gets out through a wall */
			VectorCopy( v->state.origin, from );
			from[2] += t->halfExtents[2] + 40;
			trap_Trace( &tr, from, mins, maxs, pos, cn, MASK_PLAYERSOLID );
			if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f ) {
				trap_Trace( &tr, pos, mins, maxs, pos, cn, MASK_PLAYERSOLID );
				if ( tr.startsolid || tr.allsolid ) {
					continue;
				}
				/* a clear spot, but not from above: only if the line from the center is clear */
				trap_Trace( &tr, v->state.origin, NULL, NULL, pos, v->ent->s.number, MASK_SOLID );
				if ( tr.fraction < 1.0f ) {
					continue;
				}
			}
			VectorCopy( pos, ent->client->ps.origin );
			ok = qtrue;
		}
		if ( !ok ) {
			return qfalse;		/* stuck in: stay on the seat */
		}
		VectorScale( v->state.velocity, 0.5f, ent->client->ps.velocity );
		ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
		vehExits++;
	}
	if ( ent->inuse && ent->client ) {
		ent->client->ps.pm_flags &= ~PMF_OAX_VEHICLE;
		ent->client->ps.stats[STAT_OAX_VEHICLE] = 0;
		if ( ent->health > 0 ) {
			ent->r.contents = CONTENTS_BODY;
			BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
			VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
			trap_LinkEntity( ent );
		}
	}
	if ( vc->seat == OAX_VEH_SEAT_DRIVER ) {
		oaxPhysVehicleInput_t parked;
		G_VehParkedInput( &parked );
		G_VehQueueInput( v, time, &parked );
	}
	v->occupant[vc->seat] = -1;
	if ( v->occupant[0] < 0 && ( BG_VehicleType( v->type )->numSeats < 2 || v->occupant[1] < 0 ) ) {
		v->emptySince = level.time;
	}
	vc->veh = -1;
	vc->seat = 0;
	return qtrue;
}

/* the nearest vehicle with a free seat within reach */
static gVehicle_t *G_VehNearby( gentity_t *ent, int *seatOut ) {
	gVehicle_t *best = NULL;
	float bestDist = 0;
	int i, s;

	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		const bgVehicleType_t *t;
		float d;
		if ( !v->inuse || v->dying ) {
			continue;
		}
		t = BG_VehicleType( v->type );
		d = Distance( ent->client->ps.origin, v->state.origin );
		if ( d > G_OAXVehicleReach( v->ent ) || ( best && d >= bestDist ) ) {
			continue;
		}
		for ( s = 0; s < t->numSeats; s++ ) {
			if ( v->occupant[s] < 0 ) {
				best = v;
				bestDist = d;
				*seatOut = s;
				break;
			}
		}
	}
	return best;
}

void G_OAXVehicleClientThink( gentity_t *ent, usercmd_t *ucmd ) {
	int cn = ent->s.number;
	gVehClient_t *vc;
	gclient_t *cl = ent->client;
	qboolean edge;

	if ( !gVehWorld || cn < 0 || cn >= MAX_CLIENTS ) {
		return;
	}
	vc = &gVehClients[cn];
	/* a human's command: the vehicles catch up to its time first, so its
	   pmove and its controls meet the world exactly as of that time */
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		int cap = level.time + G_VehFrameMsec();
		vehCmdTime[cn] = ucmd->serverTime;
		if ( ucmd->serverTime > cap ) {
			vehAheadCmds++;
		}
		G_VehAdvance( cap );
	}
	edge = ( ucmd->buttons & BUTTON_USE_HOLDABLE ) && !vc->useHeld;
	vc->useHeld = ( ucmd->buttons & BUTTON_USE_HOLDABLE ) != 0;
	if ( !vc->useHeld ) {
		vc->useEaten = 0;
	} else if ( vc->useEaten ) {
		ucmd->buttons &= ~BUTTON_USE_HOLDABLE;
	}

	if ( cl->ps.stats[STAT_HEALTH] <= 0 || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
		if ( vc->veh >= 0 ) {
			G_VehLeave( cn, qfalse, ucmd->serverTime );
		}
		return;
	}
	if ( vc->veh >= 0 ) {
		gVehicle_t *v = &gVehicles[vc->veh];
		if ( edge && G_VehLeave( cn, qtrue, ucmd->serverTime ) ) {
			ucmd->buttons &= ~BUTTON_USE_HOLDABLE;
			vc->useEaten = 1;
			return;
		}
		ucmd->buttons &= ~BUTTON_USE_HOLDABLE;
		if ( vc->seat == OAX_VEH_SEAT_DRIVER ) {
			if ( g_oaxVehLog.integer > 1 ) {
				G_Printf( "vehcmd %i lt %i fm %i rm %i um %i\n", ucmd->serverTime, level.time, ucmd->forwardmove, ucmd->rightmove, ucmd->upmove );
			}
			{
				oaxPhysVehicleInput_t in;
				BG_VehCmdToInput( ucmd, &in );
				G_VehQueueInput( v, ucmd->serverTime, &in );
			}
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_GESTURE );
		}
		ucmd->forwardmove = 0;
		ucmd->rightmove = 0;
		ucmd->upmove = 0;
		G_VehPlaceOccupant( v, vc->seat, ent );
		return;
	}
	cl->ps.pm_flags &= ~PMF_OAX_VEHICLE;
	cl->ps.stats[STAT_OAX_VEHICLE] = 0;
	if ( edge ) {
		int seat = 0;
		gVehicle_t *v = G_VehNearby( ent, &seat );
		if ( v ) {
			G_VehEnter( v, seat, ent, ucmd->serverTime );
			ucmd->buttons &= ~BUTTON_USE_HOLDABLE;
			vc->useEaten = 1;
			ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		}
	}
}

/* ---- life and death ------------------------------------------------------------------- */

static void G_VehPain( gentity_t *self, gentity_t *attacker, int damage ) {
	gVehicle_t *v = G_VehOfEnt( self );

	if ( v && attacker ) {
		v->attacker = attacker->s.number;
	}
}

static void G_VehDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	gVehicle_t *v = G_VehOfEnt( self );

	if ( !v || v->dying ) {
		return;
	}
	self->takedamage = qfalse;
	v->dying = 1;
	v->attacker = attacker ? attacker->s.number : ENTITYNUM_WORLD;
}

static void G_VehRemove( gVehicle_t *v, qboolean explode ) {
	int s;
	vec3_t org;

	for ( s = 0; s < OAX_VEH_MAX_SEATS; s++ ) {
		if ( v->occupant[s] >= 0 && !G_VehLeave( v->occupant[s], qtrue, level.time ) ) {
			/* no room to get out: goes down with it */
			int cn = v->occupant[s];
			G_VehLeave( cn, qfalse, level.time );
			if ( g_entities[cn].health > 0 ) {
				G_Damage( &g_entities[cn], v->ent, &g_entities[v->attacker], NULL, NULL, 1000, DAMAGE_NO_PROTECTION, MOD_CRUSH );
			}
		}
	}
	VectorCopy( v->state.origin, org );
	trap_Phys_VehicleDestroy( v->handle );
	if ( explode ) {
		gentity_t *tent = G_TempEntity( org, EV_MISSILE_MISS );
		vec3_t up = { 0, 0, 1 };
		tent->s.weapon = WP_ROCKET_LAUNCHER;
		tent->s.eventParm = DirToByte( up );
		v->ent->takedamage = qfalse;
		G_RadiusDamage( org, &g_entities[v->attacker], 120, 320, v->ent, MOD_ROCKET_SPLASH );
		vehDestroyed++;
	} else {
		vehResets++;
	}
	if ( v->spawner ) {
		v->spawner->count = -1;
		v->spawner->nextthink = level.time + (int)( v->spawner->wait * 1000.0f );
	}
	if ( BG_OAXFeature( "ent_obb" ) ) {
		trap_OAX_EntSetOBB( v->ent->s.number, NULL );
	}
	G_FreeEntity( v->ent );
	memset( v, 0, sizeof( *v ) );
}

/* ---- spawning ---------------------------------------------------------------------------- */

static void G_VehSpawn( gentity_t *spawner ) {
	const bgVehicleType_t *t = BG_VehicleType( spawner->s.generic1 );
	oaxPhysVehicleDef_t def;
	gVehicle_t *v = NULL;
	gentity_t *ent;
	int i, touch[256], n;
	vec3_t mins, maxs, origin;

	for ( i = 0; i < VEH_MAX; i++ ) {
		if ( !gVehicles[i].inuse ) {
			v = &gVehicles[i];
			break;
		}
	}
	if ( !v || !G_VehWorld() ) {
		spawner->nextthink = level.time + 1000;
		return;
	}
	/* wait while a player or another vehicle stands on the spot */
	VectorCopy( spawner->s.origin, origin );
	for ( i = 0; i < 3; i++ ) {
		mins[i] = origin[i] - t->halfExtents[0] - 16;
		maxs[i] = origin[i] + t->halfExtents[0] + 16;
	}
	n = trap_EntitiesInBox( mins, maxs, touch, 256 );
	for ( i = 0; i < n; i++ ) {
		gentity_t *o = &g_entities[touch[i]];
		if ( ( o->client && o->health > 0 && o->client->sess.sessionTeam != TEAM_SPECTATOR ) || o->s.eType == ET_OAX_VEHICLE ) {
			spawner->nextthink = level.time + 1000;
			return;
		}
	}

	{
		float quat[4];
		BG_QuatFromAngles( spawner->s.angles, quat );
		BG_VehicleDef( spawner->s.generic1, origin, quat, &def );
	}

	ent = G_Spawn();
	ent->classname = "oax_vehicle";
	ent->s.eType = ET_OAX_VEHICLE;
	ent->s.modelindex = G_ModelIndex( (char *)t->model );
	ent->s.modelindex2 = t->wheelModel[0] ? G_ModelIndex( (char *)t->wheelModel ) : 0;
	ent->s.generic1 = spawner->s.generic1;
	ent->r.contents = CONTENTS_CORPSE;	/* G_VehUpdateEntity: solid to players with g_oaxVehSolid */
	ent->r.svFlags = 0;
	ent->takedamage = qtrue;
	ent->health = t->health;
	ent->pain = G_VehPain;
	ent->die = G_VehDie;
	def.userData = ent->s.number;
	v->handle = trap_Phys_VehicleCreate( gVehWorld, &def );
	if ( !v->handle ) {
		G_FreeEntity( ent );
		spawner->nextthink = level.time + 5000;
		return;
	}
	v->inuse = 1;
	v->ent = ent;
	v->spawner = spawner;
	v->type = spawner->s.generic1;
	v->occupant[0] = v->occupant[1] = -1;
	G_VehParkedInput( &v->input );
	v->emptySince = level.time;
	v->attacker = ENTITYNUM_WORLD;
	trap_Phys_VehicleGetState( v->handle, &v->state );
	v->lastDistance = v->state.distance;
	G_VehUpdateEntity( v );
	ent->s.eFlags ^= EF_TELEPORT_BIT;
	spawner->count = ent->s.number;
	spawner->nextthink = 0;
	vehSpawned++;
}

static void G_VehSpawnerThink( gentity_t *spawner ) {
	if ( spawner->count < 0 ) {
		G_VehSpawn( spawner );
	}
}

/*QUAKED info_oax_vehicle (0 .5 1) (-16 -16 -16) (16 16 16)
A vehicle spawner for the vehicle rule (g_oaxVehicles 1).
"type"   buggy (two seats: driver and gunner) or hover (one seat)
"angle"  facing
"wait"   seconds before a destroyed vehicle comes back (20)
The origin is the chassis center: put it a little above the ground.
*/
void SP_info_oax_vehicle( gentity_t *ent ) {
	char *s;
	int type;

	G_SpawnString( "type", "buggy", &s );
	type = BG_VehicleTypeByName( s );
	if ( type < 0 || !BG_OAXFeature( "physics_vehicle" ) || !trap_Cvar_VariableIntegerValue( "g_oaxVehicles" ) ) {
		if ( type < 0 ) {
			G_Printf( S_COLOR_YELLOW "info_oax_vehicle: unknown type %s\n", s );
		}
		G_FreeEntity( ent );
		return;
	}
	G_SpawnFloat( "wait", "20", &ent->wait );
	ent->s.generic1 = type;
	ent->count = -1;
	ent->r.svFlags |= SVF_NOCLIENT;
	ent->think = G_VehSpawnerThink;
	ent->nextthink = level.time + 300;
	G_ModelIndex( (char *)BG_VehicleType( type )->model );
	if ( BG_VehicleType( type )->wheelModel[0] ) {
		G_ModelIndex( (char *)BG_VehicleType( type )->wheelModel );
	}
	gVehSpawners++;
}

/* ---- the frame ------------------------------------------------------------------------------ */

/* hard hits (walls, terrain, trees, other vehicles) damage a vehicle */
static void G_VehCrashes( void ) {
	oaxPhysContact_t ev[64];
	int i, k, n;

	n = trap_Phys_WorldContactEvents( gVehWorld, ev, 64 );
	for ( i = 0; i < n; i++ ) {
		if ( ev[i].type != PHYS_CONTACT_HIT || ev[i].speed < VEH_CRASH_SPEED ) {
			continue;
		}
		for ( k = 0; k < 2; k++ ) {
			int num = k ? ev[i].userB : ev[i].userA;
			gVehicle_t *v;
			if ( num < MAX_CLIENTS || num >= ENTITYNUM_MAX_NORMAL || !( v = G_VehOfEnt( &g_entities[num] ) ) || v->dying ) {
				continue;
			}
			G_Damage( v->ent, NULL, NULL, NULL, NULL, (int)( ( ev[i].speed - VEH_CRASH_SPEED ) * 0.15f ), DAMAGE_NO_KNOCKBACK, MOD_CRUSH );
			vehCrashes++;
		}
	}
}

static void G_VehRoadkill( gVehicle_t *v ) {
	const bgVehicleType_t *t = BG_VehicleType( v->type );
	float speed = VectorLength( v->state.velocity );
	int touch[256], n, i;
	vec3_t mins, maxs;

	if ( speed < VEH_ROADKILL_SPEED ) {
		return;
	}
	VectorAdd( v->state.origin, v->ent->r.mins, mins );
	VectorAdd( v->state.origin, v->ent->r.maxs, maxs );
	for ( i = 0; i < 3; i++ ) {
		mins[i] -= 24;
		maxs[i] += 24;
	}
	n = trap_EntitiesInBox( mins, maxs, touch, 256 );
	for ( i = 0; i < n; i++ ) {
		gentity_t *o = &g_entities[touch[i]];
		vec3_t d, dir;
		float lx, ly, lz;
		int dmg, k, aboard = 0;
		if ( !o->client || o->health <= 0 || o->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		for ( k = 0; k < OAX_VEH_MAX_SEATS; k++ ) {
			if ( v->occupant[k] == o->s.number ) {
				aboard = 1;
			}
		}
		if ( aboard || gVehClients[o->s.number].veh >= 0 || level.time - gVehClients[o->s.number].lastRoadkill < 500 ) {
			continue;
		}
		VectorSubtract( o->r.currentOrigin, v->state.origin, d );
		lx = DotProduct( d, v->axis[0] );
		ly = DotProduct( d, v->axis[1] );
		lz = DotProduct( d, v->axis[2] );
		if ( fabs( lx ) > t->halfExtents[0] + 16 || fabs( ly ) > t->halfExtents[1] + 16 ||
			lz < -t->halfExtents[2] - 56 || lz > t->halfExtents[2] + 24 ) {
			continue;
		}
		gVehClients[o->s.number].lastRoadkill = level.time;
		dmg = (int)( ( speed - 200.0f ) * 0.2f );
		if ( dmg > 250 ) {
			dmg = 250;
		}
		VectorNormalize2( v->state.velocity, dir );
		G_Damage( o, v->ent, v->occupant[0] >= 0 ? &g_entities[v->occupant[0]] : NULL, dir, o->r.currentOrigin, dmg, 0, MOD_CRUSH );
		vehRoadkills++;
	}
}

static void G_VehHashState( gVehicle_t *v ) {
	floatint_t fi;
	int i;

	/* the drive hash: every tick from the first driver on, keyed by the
	   world time since the get-in command, so it does not depend on how
	   long the vehicle stood parked */
	if ( v->driveStart ) {
		int rel = vehWorldTime - v->driveStart;
		if ( rel >= 0 ) {
			vehDriveHash = ( vehDriveHash ^ (unsigned)rel ) * 16777619U;
			vehDriveHash = ( vehDriveHash ^ (unsigned)( v - gVehicles ) ) * 16777619U;
			for ( i = 0; i < 3; i++ ) {
				fi.f = v->state.origin[i];
				vehDriveHash = ( vehDriveHash ^ (unsigned)fi.i ) * 16777619U;
				fi.f = v->state.velocity[i];
				vehDriveHash = ( vehDriveHash ^ (unsigned)fi.i ) * 16777619U;
			}
			for ( i = 0; i < 4; i++ ) {
				fi.f = v->state.quat[i];
				vehDriveHash = ( vehDriveHash ^ (unsigned)fi.i ) * 16777619U;
			}
		}
		if ( g_oaxVehLog.integer ) {
			G_Printf( "vehlog %i %i %i %i: in %.3f %.3f %.2f %.2f at %.3f %.3f %.3f v %.3f %.3f %.3f %08x\n", (int)( v - gVehicles ), rel, vehWorldTime, level.time,
				v->input.throttle, v->input.steer, v->input.brake, v->input.handbrake,
				v->state.origin[0], v->state.origin[1], v->state.origin[2],
				v->state.velocity[0], v->state.velocity[1], v->state.velocity[2], vehDriveHash );
		}
		/* checkpoints: the first vehicle anyone drove, its first 15 s (debug
		   values are a small table) */
		if ( vehCheckpointVeh < 0 ) {
			vehCheckpointVeh = (int)( v - gVehicles );
		}
		if ( v == &gVehicles[vehCheckpointVeh] && rel >= v->nextCheckpoint && v->nextCheckpoint <= 15000 ) {
			BG_OAXDebugSet( va( "g_veh_drive_%i_t%i", (int)( v - gVehicles ), v->nextCheckpoint / 1000 ),
				va( "%.3f %.3f %.3f %.3f %.3f %.3f %.3f %08x", v->state.origin[0], v->state.origin[1], v->state.origin[2],
					v->state.speed, v->state.distance, v->state.steer, v->state.suspension[0], vehDriveHash ) );
			v->nextCheckpoint += 1000;
			/* the drive hash as of the last checkpoint: the same prefix on every build */
			BG_OAXDebugSet( "g_veh_drive_hash", va( "%08x", vehDriveHash ) );
		}
	}

	for ( i = 0; i < 3; i++ ) {
		fi.f = v->state.origin[i];
		vehHash = ( vehHash ^ (unsigned)fi.i ) * 16777619U;
		fi.f = v->state.velocity[i];
		vehHash = ( vehHash ^ (unsigned)fi.i ) * 16777619U;
	}
	for ( i = 0; i < 4; i++ ) {
		fi.f = v->state.quat[i];
		vehHash = ( vehHash ^ (unsigned)fi.i ) * 16777619U;
	}
}

/* ---- the clock ------------------------------------------------------------------------------ */

static int G_VehFrameMsec( void ) {
	int msec = level.time - level.previousTime;
	return msec > 0 ? msec : 50;
}

/* how far the world may run: up to cap, but never past the last command of
   a human in the game (their next commands are still on the way) */
static int G_VehClockLimit( int cap ) {
	int i, limit = cap;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *e = &g_entities[i];
		int t;
		if ( !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED || ( e->r.svFlags & SVF_BOT ) ||
			e->client->sess.sessionTeam == TEAM_SPECTATOR || !vehCmdTime[i] ) {
			continue;
		}
		t = vehCmdTime[i];
		if ( t < level.time - VEH_MAX_LAG ) {
			t = level.time - VEH_MAX_LAG;
		}
		if ( t < limit ) {
			limit = t;
		}
	}
	return limit;
}

/* carry the players standing on the deck with the vehicle */
static void G_VehCarry( gVehicle_t *v ) {
	int i, k, contents = v->ent->r.contents;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *e = &g_entities[i];
		gclient_t *cl = e->client;
		vec3_t d, local, to;
		trace_t tr;
		if ( !e->inuse || !cl || e->health <= 0 || cl->sess.sessionTeam == TEAM_SPECTATOR || gVehClients[i].veh >= 0 ||
			cl->ps.groundEntityNum != v->ent->s.number || cl->ps.pm_type != PM_NORMAL ) {
			continue;
		}
		VectorSubtract( cl->ps.origin, v->prevOrigin, d );
		for ( k = 0; k < 3; k++ ) {
			local[k] = DotProduct( d, v->prevAxis[k] );
		}
		BG_VehLocalToWorld( v->state.origin, v->axis, local, to );
		v->ent->r.contents = 0;
		trap_Trace( &tr, cl->ps.origin, e->r.mins, e->r.maxs, to, i, MASK_PLAYERSOLID );
		v->ent->r.contents = contents;
		if ( tr.startsolid ) {
			continue;
		}
		VectorCopy( tr.endpos, cl->ps.origin );
		VectorCopy( cl->ps.origin, e->r.currentOrigin );
		BG_PlayerStateToEntityState( &cl->ps, &e->s, qtrue );
		trap_LinkEntity( e );
		vehCarried++;
	}
}

/* push the players a moving vehicle drove into out of its box, sideways
   along the vehicle axis they are least deep on, with its speed that way */
static void G_VehPush( gVehicle_t *v ) {
	const bgVehicleType_t *t = BG_VehicleType( v->type );
	int touch[MAX_GENTITIES], n, i, k, contents = v->ent->r.contents;
	float obb[15];
	vec3_t center;

	if ( contents != CONTENTS_BODY ) {
		return;
	}
	BG_VehOBB( v->type, v->axis, obb );
	VectorAdd( v->state.origin, obb, center );
	n = trap_EntitiesInBox( v->ent->r.absmin, v->ent->r.absmax, touch, MAX_GENTITIES );
	for ( i = 0; i < n; i++ ) {
		gentity_t *o = &g_entities[touch[i]];
		gclient_t *cl = o->client;
		vec3_t amin, amax, pc, d, dir, to;
		float pen[2], hp[3], r, dk, step, vd, pd;
		trace_t tr;
		int best, s;
		if ( !cl || o->health <= 0 || cl->sess.sessionTeam == TEAM_SPECTATOR || gVehClients[o->s.number].veh >= 0 ||
			!( o->r.contents & CONTENTS_BODY ) || cl->ps.groundEntityNum == v->ent->s.number ) {
			continue;
		}
		VectorAdd( cl->ps.origin, o->r.mins, amin );
		VectorAdd( cl->ps.origin, o->r.maxs, amax );
		if ( !trap_EntityContact( amin, amax, v->ent ) ) {
			continue;
		}
		for ( k = 0; k < 3; k++ ) {
			pc[k] = ( amin[k] + amax[k] ) * 0.5f;
			hp[k] = ( amax[k] - amin[k] ) * 0.5f;
		}
		VectorSubtract( pc, center, d );
		for ( k = 0; k < 2; k++ ) {
			const float *a = v->axis[k];
			r = hp[0] * fabs( a[0] ) + hp[1] * fabs( a[1] ) + hp[2] * fabs( a[2] );
			dk = DotProduct( d, a );
			pen[k] = t->colHalf[k] + r - fabs( dk );
		}
		best = pen[1] < pen[0] ? 1 : 0;
		dk = DotProduct( d, v->axis[best] );
		dir[0] = v->axis[best][0];
		dir[1] = v->axis[best][1];
		dir[2] = 0;
		if ( dk < 0 ) {
			VectorNegate( dir, dir );
		}
		if ( VectorNormalize( dir ) < 0.1f ) {
			continue;
		}
		/* out, a few units at a time past the computed depth if the tilt fools it */
		for ( s = 0; s < 6; s++ ) {
			step = pen[best] + 1.0f + s * 4.0f;
			VectorMA( cl->ps.origin, step, dir, to );
			VectorAdd( to, o->r.mins, amin );
			VectorAdd( to, o->r.maxs, amax );
			if ( !trap_EntityContact( amin, amax, v->ent ) ) {
				break;
			}
		}
		v->ent->r.contents = 0;
		trap_Trace( &tr, cl->ps.origin, o->r.mins, o->r.maxs, to, o->s.number, MASK_PLAYERSOLID );
		v->ent->r.contents = contents;
		if ( s == 6 || tr.startsolid || tr.fraction < 1.0f ) {
			vehBlockedPush++;		/* pinned against something: the roadkill rule decides */
			continue;
		}
		VectorCopy( tr.endpos, cl->ps.origin );
		vd = DotProduct( v->state.velocity, dir );
		pd = DotProduct( cl->ps.velocity, dir );
		if ( pd < vd ) {
			VectorMA( cl->ps.velocity, vd - pd, dir, cl->ps.velocity );
		}
		VectorCopy( cl->ps.origin, o->r.currentOrigin );
		BG_PlayerStateToEntityState( &cl->ps, &o->s, qtrue );
		trap_LinkEntity( o );
		vehPushed++;
	}
}

static gVehicle_t *G_VehOfNum( int num ) {
	if ( num < 0 || num >= MAX_GENTITIES ) {
		return NULL;
	}
	return G_VehOfEnt( &g_entities[num] );
}

/* one physics tick: controls and impulses for the tick that starts at
   vehWorldTime, the step, then everything that follows from it */
static void G_VehTick( void ) {
	oaxPhysBodyState_t bs;
	trace_t tr;
	int i, k, t0 = vehWorldTime;

	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		if ( !v->inuse || v->dying ) {
			continue;
		}
		G_VehTakeInput( v, t0 );
		trap_Phys_VehicleSetInput( v->handle, &v->input );
		VectorCopy( v->state.origin, v->prevOrigin );
		VectorCopy( v->axis[0], v->prevAxis[0] );
		VectorCopy( v->axis[1], v->prevAxis[1] );
		VectorCopy( v->axis[2], v->prevAxis[2] );
	}
	/* hits up to the start of this tick, in the order they came */
	for ( i = 0, k = 0; i < vehNumImpulses; i++ ) {
		gVehImpulse_t *im = &vehImpulses[i];
		gVehicle_t *v;
		if ( im->time > t0 ) {
			vehImpulses[k++] = *im;
			continue;
		}
		v = G_VehOfNum( im->ent );
		if ( v && !v->dying ) {
			/* at the hit point pulled into the chassis as it is now */
			const bgVehicleType_t *t = BG_VehicleType( v->type );
			vec3_t d, local, at;
			int a;
			VectorSubtract( im->point, v->state.origin, d );
			for ( a = 0; a < 3; a++ ) {
				local[a] = DotProduct( d, v->axis[a] );
				if ( local[a] > t->halfExtents[a] ) {
					local[a] = t->halfExtents[a];
				} else if ( local[a] < -t->halfExtents[a] ) {
					local[a] = -t->halfExtents[a];
				}
			}
			BG_VehLocalToWorld( v->state.origin, v->axis, local, at );
			trap_Phys_BodyApply( v->state.body, PHYS_APPLY_IMPULSE, im->impulse, at );
		}
	}
	vehNumImpulses = k;

	trap_Phys_WorldStep( gVehWorld, -1 );
	vehWorldTime += VEH_TICK;
	G_VehCrashes();

	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		float moved;
		int s;
		if ( !v->inuse || v->dying ) {
			continue;
		}
		trap_Phys_VehicleGetState( v->handle, &v->state );
		v->awake = trap_Phys_BodyGetState( v->state.body, &bs ) && ( bs.flags & PHYS_STATE_AWAKE );
		G_VehUpdateEntity( v );
		for ( s = 0; s < OAX_VEH_MAX_SEATS; s++ ) {
			if ( v->occupant[s] >= 0 ) {
				G_VehPlaceOccupant( v, s, &g_entities[v->occupant[s]] );
			}
		}
		G_VehCarry( v );
		G_VehPush( v );
		G_VehRoadkill( v );
		vehFrames++;
		moved = v->state.distance - v->lastDistance;
		v->lastDistance = v->state.distance;
		if ( v->occupant[0] >= 0 ) {
			vehDrivenFrames++;
			vehDistance += moved;
			if ( g_entities[v->occupant[0]].r.svFlags & SVF_BOT ) {
				vehBotDrivenFrames++;
				vehBotDistance += moved;
			}
		}
		if ( v->occupant[1] >= 0 ) {
			vehGunnerFrames++;
		}
		if ( v->state.origin[2] < vehMinZ ) {
			vehMinZ = v->state.origin[2];
		}
		/* a point trace: trap_PointContents would count trigger brushes
		   (compiled CONTENTS_SOLID) as solid */
		trap_Trace( &tr, v->state.origin, NULL, NULL, v->state.origin, v->ent->s.number, CONTENTS_SOLID );
		if ( tr.startsolid ) {
			vehInSolid++;
		}
		G_VehHashState( v );
	}
}

/* run the world up to cap (or as far as the humans' commands allow) */
static void G_VehAdvance( int cap ) {
	int limit, n = 0;

	if ( !gVehWorld ) {
		return;
	}
	limit = G_VehClockLimit( cap );
	if ( limit < cap && vehWorldTime + VEH_TICK <= cap && vehWorldTime + VEH_TICK > limit ) {
		vehHeldTicks++;		/* a human's commands hold the clock back */
	}
	while ( vehWorldTime + VEH_TICK <= limit && n < 64 ) {
		G_VehTick();
		n++;
	}
}

/* G_Damage: a hit or an explosion pushes the vehicle. The impulse acts at
   the hit point (pulled into the vehicle's box), at the first tick that
   starts at or after now */
void G_OAXVehicleImpulse( gentity_t *veh, const vec3_t dir, const vec3_t point, int knockback ) {
	gVehicle_t *v = G_VehOfEnt( veh );
	gVehImpulse_t *im;

	if ( !v || v->dying || !dir || knockback <= 0 || vehNumImpulses >= VEH_MAX_IMPULSES ) {
		return;
	}
	im = &vehImpulses[vehNumImpulses++];
	im->time = level.time;
	im->ent = veh->s.number;
	VectorScale( dir, g_knockback.value * knockback * VEH_KNOCKBACK_SCALE, im->impulse );
	if ( point ) {
		VectorCopy( point, im->point );
	} else {
		VectorCopy( v->state.origin, im->point );
	}
	vehImpulseCount++;
	vehImpulseTotal += VectorLength( im->impulse );
}

static void G_VehPublish( void ) {
	oaxPhysStats_t st;
	int k, i, live = 0, driven = 0, botDriven = 0, gunned = 0;

	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		if ( !v->inuse || v->dying ) {
			continue;
		}
		live++;
		if ( v->occupant[0] >= 0 ) {
			driven++;
			if ( g_entities[v->occupant[0]].r.svFlags & SVF_BOT ) {
				botDriven++;
			}
		}
		if ( v->occupant[1] >= 0 ) {
			gunned++;
		}
	}
	memset( &st, 0, sizeof( st ) );
	trap_Phys_WorldStats( gVehWorld, &st );
	BG_OAXDebugSetInt( "g_veh_spawners", gVehSpawners );
	BG_OAXDebugSetInt( "g_veh_count", live );
	BG_OAXDebugSetInt( "g_veh_driven", driven );
	BG_OAXDebugSetInt( "g_veh_bot_driven", botDriven );
	BG_OAXDebugSetInt( "g_veh_gunned", gunned );
	BG_OAXDebugSetInt( "g_veh_spawned", vehSpawned );
	BG_OAXDebugSetInt( "g_veh_destroyed", vehDestroyed );
	BG_OAXDebugSetInt( "g_veh_resets", vehResets );
	BG_OAXDebugSetInt( "g_veh_enters", vehEnters );
	BG_OAXDebugSetInt( "g_veh_bot_enters", vehBotEnters );
	BG_OAXDebugSetInt( "g_veh_exits", vehExits );
	BG_OAXDebugSetInt( "g_veh_roadkills", vehRoadkills );
	BG_OAXDebugSetInt( "g_veh_late_cmds", vehLateCmds );
	BG_OAXDebugSetInt( "g_veh_crashes", vehCrashes );
	BG_OAXDebugSetInt( "g_veh_gunner_shots", vehGunnerShots );
	/* the first client: vehicle, seat, weapon, its ammo (seat tests) */
	if ( g_entities[0].inuse && g_entities[0].client ) {
		gclient_t *cl = g_entities[0].client;
		BG_OAXDebugSet( "g_veh_p0", va( "%i %i %i %i", G_OAXVehicleOfClient( 0, &i ), gVehClients[0].veh >= 0 ? gVehClients[0].seat : -1,
			cl->ps.weapon, cl->ps.ammo[cl->ps.weapon] ) );
		/* where it stands, and on what (riding a deck: the vehicle's number) */
		BG_OAXDebugSet( "g_veh_p0_pos", va( "%.3f %.3f %.3f %i", cl->ps.origin[0], cl->ps.origin[1], cl->ps.origin[2], cl->ps.groundEntityNum ) );
	}
	BG_OAXDebugSetInt( "g_veh_cmd_lag_max", vehCmdLagMax );
	BG_OAXDebugSetInt( "g_veh_ahead_cmds", vehAheadCmds );
	BG_OAXDebugSetInt( "g_veh_held_ticks", vehHeldTicks );
	BG_OAXDebugSetInt( "g_veh_world_lag", level.time - vehWorldTime );
	BG_OAXDebugSetInt( "g_veh_impulses", vehImpulseCount );
	BG_OAXDebugSet( "g_veh_impulse_total", va( "%.0f", vehImpulseTotal ) );
	BG_OAXDebugSetInt( "g_veh_carried", vehCarried );
	BG_OAXDebugSetInt( "g_veh_pushed", vehPushed );
	BG_OAXDebugSetInt( "g_veh_push_blocked", vehBlockedPush );
	BG_OAXDebugSetInt( "g_veh_solid", g_oaxVehSolid.integer && BG_OAXFeature( "ent_obb" ) );
	BG_OAXDebugSetInt( "g_veh_frames", vehFrames );
	BG_OAXDebugSetInt( "g_veh_driven_frames", vehDrivenFrames );
	BG_OAXDebugSetInt( "g_veh_bot_driven_frames", vehBotDrivenFrames );
	BG_OAXDebugSetInt( "g_veh_gunner_frames", vehGunnerFrames );
	BG_OAXDebugSetInt( "g_veh_in_solid", vehInSolid );
	BG_OAXDebugSetInt( "g_veh_fell", vehFell );
	BG_OAXDebugSet( "g_veh_min_z", vehMinZ < 1e9f ? va( "%.1f", vehMinZ ) : "none" );
	BG_OAXDebugSet( "g_veh_distance", va( "%.1f", vehDistance ) );
	BG_OAXDebugSet( "g_veh_bot_distance", va( "%.1f", vehBotDistance ) );
	BG_OAXDebugSet( "g_veh_hash", va( "%08x", vehHash ) );
	BG_OAXDebugSetInt( "g_veh_phys_ticks", st.ticks );
	BG_OAXDebugSetInt( "g_veh_phys_bodies", st.bodies );
	BG_OAXDebugSetInt( "g_veh_phys_workers", st.workerCount );
	BG_OAXDebugSetInt( "g_veh_terrain_bodies", vehTerrainBodies );
	BG_OAXDebugSet( "g_veh_phys_ms", va( "%.3f", st.lastStepMs ) );
	BG_OAXDebugSet( "g_veh_phys_world_hash", va( "%08x", st.hash ) );
	/* the first vehicles, for scripted tests (g_veh0 is the first) */
	for ( i = 0, k = 0; i < VEH_MAX && k < 4; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		if ( v->inuse && !v->dying ) {
			BG_OAXDebugSet( va( "g_veh%i", k ), va( "%i %.2f %.2f %.2f %.2f %.1f %i %i", v->ent->s.number,
				v->state.origin[0], v->state.origin[1], v->state.origin[2], v->state.speed, v->state.distance,
				v->state.contacts, v->ent->health ) );
			/* bit-exact state for identity checks (impulse test) */
			BG_OAXDebugSet( va( "g_veh%i_exact", k ), va( "%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %i", v->state.origin[0], v->state.origin[1], v->state.origin[2],
				v->state.velocity[0], v->state.velocity[1], v->state.velocity[2],
				v->state.quat[0], v->state.quat[1], v->state.quat[2], v->state.quat[3], vehWorldTime ) );
			k++;
		}
	}
}

/* before the level world steps (G_OAXPhysFrame): seats and controls */
void G_OAXVehicleFrameBegin( void ) {
	int i;

	if ( !gVehWorld ) {
		return;
	}
	trap_Cvar_Update( &g_oaxVehLog );
	trap_Cvar_Update( &g_oaxVehSolid );

	/* occupants that died, left or went to spectate since last frame */
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( gVehClients[i].veh < 0 ) {
			continue;
		}
		if ( !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED || e->health <= 0 ||
			e->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			G_VehLeave( i, qfalse, level.time );
		}
	}

	/* the world up to this frame's level time, as far as the humans'
	   commands allow (their ClientThink takes it the rest of the way) */
	G_VehAdvance( level.time );
}

/* after the step: entities, occupants, statistics */
void G_OAXVehicleFrame( void ) {
	int i, s;

	if ( !gVehWorld ) {
		return;
	}
	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		if ( !v->inuse ) {
			continue;
		}
		if ( v->dying ) {
			G_VehRemove( v, qtrue );
			continue;
		}
		/* health and seats changed since the last tick */
		G_VehUpdateEntity( v );
		/* out of the world: gone */
		if ( v->state.origin[2] < VEH_KILL_Z ) {
			vehFell++;
			G_VehRemove( v, qfalse );
			continue;
		}
		/* empty and away from home, or empty on its roof: back to the spawner */
		if ( v->occupant[0] < 0 && v->occupant[1] < 0 ) {
			if ( v->axis[2][2] < 0.3f ) {
				if ( !v->upsideSince ) {
					v->upsideSince = level.time;
				}
			} else {
				v->upsideSince = 0;
			}
			if ( ( v->emptySince && level.time - v->emptySince > VEH_ABANDON_MS && v->spawner &&
				Distance( v->state.origin, v->spawner->s.origin ) > 600.0f ) ||
				( v->upsideSince && level.time - v->upsideSince > 10000 ) ) {
				G_VehRemove( v, qfalse );
				continue;
			}
		} else {
			v->upsideSince = 0;
		}
		for ( s = 0; s < OAX_VEH_MAX_SEATS; s++ ) {
			if ( v->occupant[s] >= 0 ) {
				gclient_t *cl = g_entities[v->occupant[s]].client;
				G_VehPlaceOccupant( v, s, &g_entities[v->occupant[s]] );
				/* shots fired from the gunner seat */
				if ( s == OAX_VEH_SEAT_GUNNER ) {
					int ammo = cl->ps.ammo[cl->ps.weapon];
					if ( vehGunAmmo[v->occupant[s]] > ammo ) {
						vehGunnerShots += vehGunAmmo[v->occupant[s]] - ammo;
					}
					vehGunAmmo[v->occupant[s]] = ammo;
				}
			}
		}
	}
	if ( level.time >= vehNextHashTime ) {
		BG_OAXDebugSet( va( "g_veh_hash_%i", vehNextHashTime / 1000 ), va( "%08x", vehHash ) );
		vehNextHashTime += 30000;
	}
	G_VehPublish();
	G_OAXVehBotFrame();
}

void G_OAXVehicleInit( void ) {
	int i;

	trap_Cvar_Register( &g_oaxVehicles, "g_oaxVehicles", "0", CVAR_SERVERINFO | CVAR_LATCH );
	trap_Cvar_Register( &g_oaxVehLog, "g_oaxVehLog", "0", 0 );
	trap_Cvar_Register( &g_oaxVehSolid, "g_oaxVehSolid", "1", CVAR_CHEAT );
	vehLateCmds = vehCmdLagMax = vehCrashes = vehGunnerShots = 0;
	vehAheadCmds = vehHeldTicks = vehImpulseCount = vehCarried = vehPushed = vehBlockedPush = 0;
	vehImpulseTotal = 0;
	vehNumImpulses = 0;
	vehWorldTime = 0;
	memset( vehCmdTime, 0, sizeof( vehCmdTime ) );
	memset( gVehicles, 0, sizeof( gVehicles ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gVehClients[i].veh = -1;
		gVehClients[i].seat = 0;
		gVehClients[i].useHeld = 0;
		gVehClients[i].useEaten = 0;
		gVehClients[i].lastRoadkill = -100000;
	}
	gVehWorld = 0;
	vehSpawned = vehDestroyed = vehResets = vehEnters = vehBotEnters = vehExits = vehRoadkills = 0;
	vehInSolid = vehFell = vehFrames = vehDrivenFrames = vehBotDrivenFrames = vehGunnerFrames = 0;
	vehDistance = vehBotDistance = 0;
	vehMinZ = 1e10f;
	vehHash = 2166136261U;
	vehDriveHash = 2166136261U;
	vehCheckpointVeh = -1;
	vehNextHashTime = 0;
	/* the spawners ran before this (G_SpawnEntitiesFromString): a world
	   only when the map has vehicles and the rule is on */
	if ( gVehSpawners > 0 ) {
		G_VehWorld();
		G_OAXVehBotInit();
		G_Printf( "vehicles: %i spawners, physics world %i\n", gVehSpawners, gVehWorld );
	}
}

/*
=================
G_OAXVehSeat_f

vehseat <client> <seat>: put a client on that seat of the nearest vehicle
where it is free (server console; tests).
=================
*/
void G_OAXVehSeat_f( void ) {
	char arg[16];
	gentity_t *ent;
	gVehicle_t *best = NULL;
	float bestDist = 0;
	int cn, seat, i;

	if ( !gVehWorld ) {
		G_Printf( "vehseat: no vehicles\n" );
		return;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	cn = atoi( arg );
	trap_Argv( 2, arg, sizeof( arg ) );
	seat = atoi( arg );
	if ( cn < 0 || cn >= level.maxclients || seat < 0 || seat >= OAX_VEH_MAX_SEATS ) {
		G_Printf( "usage: vehseat <client> <seat 0 driver, 1 gunner>\n" );
		return;
	}
	ent = &g_entities[cn];
	if ( !ent->inuse || !ent->client || ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	if ( gVehClients[cn].veh >= 0 ) {
		G_VehLeave( cn, qfalse, level.time );
	}
	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		float d;
		if ( !v->inuse || v->dying || seat >= BG_VehicleType( v->type )->numSeats || v->occupant[seat] >= 0 ) {
			continue;
		}
		d = Distance( ent->client->ps.origin, v->state.origin );
		if ( !best || d < bestDist ) {
			best = v;
			bestDist = d;
		}
	}
	if ( best ) {
		G_VehEnter( best, seat, ent, level.time );
	}
}

/*
=================
G_OAXVehDrive_f

vehdrive <throttle> <steer> <msec> [n]: drive the first vehicle that has no
driver (or the n-th vehicle) with these controls for msec, then park it
(server console; tests: riding the deck, pushing). Queued like a driver's
commands, from now on.
=================
*/
void G_OAXVehDrive_f( void ) {
	char arg[16];
	oaxPhysVehicleInput_t in, parked;
	int i, msec;

	if ( !gVehWorld ) {
		G_Printf( "vehdrive: no vehicles\n" );
		return;
	}
	memset( &in, 0, sizeof( in ) );
	trap_Argv( 1, arg, sizeof( arg ) );
	in.throttle = atof( arg );
	trap_Argv( 2, arg, sizeof( arg ) );
	in.steer = atof( arg );
	trap_Argv( 3, arg, sizeof( arg ) );
	msec = atoi( arg );
	G_VehParkedInput( &parked );
	if ( trap_Argc() > 4 ) {
		gentity_t *e;
		trap_Argv( 4, arg, sizeof( arg ) );
		e = G_OAXVehicleEnt( atoi( arg ) );
		if ( e && G_VehOfEnt( e ) && G_VehOfEnt( e )->occupant[OAX_VEH_SEAT_DRIVER] < 0 ) {
			G_VehQueueInput( G_VehOfEnt( e ), level.time, &in );
			G_VehQueueInput( G_VehOfEnt( e ), level.time + msec, &parked );
		}
		return;
	}
	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		if ( v->inuse && !v->dying && v->occupant[OAX_VEH_SEAT_DRIVER] < 0 ) {
			G_VehQueueInput( v, level.time, &in );
			G_VehQueueInput( v, level.time + msec, &parked );
			return;
		}
	}
}

/*
=================
G_OAXVehRocket_f

vehrocket <x y z> <tx ty tz>: a real rocket (fire_rocket, the world its
owner) from x y z toward tx ty tz (server console; tests: an explosion next
to a parked vehicle).
=================
*/
void G_OAXVehRocket_f( void ) {
	char arg[32];
	vec3_t start, target, dir;
	int i;

	if ( trap_Argc() < 7 ) {
		G_Printf( "usage: vehrocket <x y z> <tx ty tz>\n" );
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		trap_Argv( 1 + i, arg, sizeof( arg ) );
		start[i] = atof( arg );
		trap_Argv( 4 + i, arg, sizeof( arg ) );
		target[i] = atof( arg );
	}
	VectorSubtract( target, start, dir );
	VectorNormalize( dir );
	fire_rocket( &g_entities[ENTITYNUM_WORLD], start, dir );
}

/*
=================
G_OAXVehPlace_f

vehplace <client> <x y z>: put a client there standing still (no
teleporter push, unlike setviewpos; server console; tests: standing on a
vehicle's deck).
=================
*/
void G_OAXVehPlace_f( void ) {
	char arg[32];
	gentity_t *ent;
	int cn, i;

	if ( trap_Argc() < 5 ) {
		G_Printf( "usage: vehplace <client> <x y z>\n" );
		return;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	cn = atoi( arg );
	if ( cn < 0 || cn >= level.maxclients ) {
		return;
	}
	ent = &g_entities[cn];
	if ( !ent->inuse || !ent->client || ent->health <= 0 || gVehClients[cn].veh >= 0 ) {
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		trap_Argv( 2 + i, arg, sizeof( arg ) );
		ent->client->ps.origin[i] = atof( arg );
	}
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.groundEntityNum = ENTITYNUM_NONE;
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

/*
=================
G_OAXVehKick_f

vehkick <ix iy iz>: an impulse (kg u/s) at the center of the first vehicle
with a driver (else the first vehicle), queued like an explosion's (server
console; tests: something the driving client cannot predict).
=================
*/
void G_OAXVehKick_f( void ) {
	char arg[32];
	gVehicle_t *best = NULL;
	gVehImpulse_t *im;
	int i;

	for ( i = 0; i < VEH_MAX; i++ ) {
		gVehicle_t *v = &gVehicles[i];
		if ( v->inuse && !v->dying && ( !best || ( best->occupant[0] < 0 && v->occupant[0] >= 0 ) ) ) {
			best = v;
		}
	}
	if ( !best || vehNumImpulses >= VEH_MAX_IMPULSES ) {
		return;
	}
	im = &vehImpulses[vehNumImpulses++];
	im->time = level.time;
	im->ent = best->ent->s.number;
	for ( i = 0; i < 3; i++ ) {
		trap_Argv( 1 + i, arg, sizeof( arg ) );
		im->impulse[i] = atof( arg );
	}
	VectorCopy( best->state.origin, im->point );
	vehImpulseCount++;
	vehImpulseTotal += VectorLength( im->impulse );
}

void G_OAXVehicleShutdown( void ) {
	/* the engine drops the game VM's worlds and vehicles with the VM */
	gVehWorld = 0;
	gVehSpawners = 0;
}
