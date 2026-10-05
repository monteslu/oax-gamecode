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
cg_oax_vehicle.c: drawing vehicles, the view from a seat, the driver's
own-vehicle prediction, collision with vehicles in the player's own
prediction, and the vehicle HUD.

Every ET_OAX_VEHICLE entity carries the server's physics state of the
vehicle bit for bit, with the world time of that state
(BG_VehStateToEntity). Other players' vehicles are drawn between the last
two snapshots like any interpolated entity; the wheels come from the type
table (bg_oax_vehicle.c) with the suspension lengths, steering and roll
the entity carries (origin2, angles2), interpolated the same way.

Own-vehicle prediction (cg_oaxVehPredict 1, engine token
"physics_vehicle_state"): the driver runs the same vehicle in a Box3D
world of its own (the map's brushes, patches and terrain, as the server's
world has them) with the same physics code. When a snapshot brings a new
authoritative state, the predicted vehicle is set to it and the driver's
commands after it are replayed tick by tick with the server's rule (the
tick that starts at T drives with the last command at or before T); in
between, new commands step it on. The vehicle is drawn at the newest
command time. Where the replay from the new state disagrees with what was
shown, the difference becomes an error offset that decays over
cg_oaxVehErrorDecay ms, so corrections never snap (unless over
cg_oaxVehSnap units: a teleport). The server stays authoritative; nothing
here feeds back into the game.

Debug values (tests): cg_vehpred_* (on, prediction error at each new
server state: max and mean distance; corrections: count, per second, max
and mean; snaps; tracking: how far what was drawn at a command time was
from the server's vehicle at that time, max and mean).

Views (cg_oaxVehView, the toggleview command): first person looks out
from the seat, or from the cockpit of a vehicle that has one (its driver
sits inside: hidden, the eye in the type table); third person is a chase
camera behind the vehicle. By default the driver of a cockpit vehicle and
every gunner see first person, the driver of an open vehicle the chase
camera. Whoever fires the vehicle's mounted gun aims it with the view
(the camera orbits with the aim, the gun turns to it within its limits,
and the vehicle steers on its own). A seat without the gun looks around
freely relative to the hull, and the view swings back behind the vehicle
when the stick is let go.

Mounted guns: drawn at their mount turned to the aim of whoever fires
them; the entity's shot count (powerups) flashes the muzzle and plays the
shot, its heat (clientNum) shows on the gun user's HUD. The gun user's
reticle is where the gun's line meets the world, not the middle of the
screen (the camera and the muzzle see from different places).
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_oax_phys.h"
#include "../game/bg_oax_vehicle.h"

int		trap_Phys_VehicleCreate( int world, const oaxPhysVehicleDef_t *def );
void	trap_Phys_VehicleDestroy( int vehicle );
void	trap_Phys_VehicleSetInput( int vehicle, const oaxPhysVehicleInput_t *in );
int		trap_Phys_VehicleGetState( int vehicle, oaxPhysVehicleState_t *out );
int		trap_Phys_VehicleSetState( int vehicle, const oaxPhysVehicleState_t *in, int flags );
int		trap_Phys_WorldAddTerrain( int world, const oaxPhysShapeDef_t *material, int *bodies, int max );
clipHandle_t trap_OAX_CM_TempOBBModel( const float *obb, int contents );

#define VP_RING			256		/* predicted states and drawn positions kept (power of two) */
#define VP_MAX_REPLAY	64		/* ticks one frame may replay */

static vmCvar_t	cg_oaxVehPredict;
static vmCvar_t	cg_oaxVehErrorDecay;
static vmCvar_t	cg_oaxVehSnap;
static vmCvar_t	cg_oaxVehHud;
static vmCvar_t	cg_oaxVehClip;	/* 0: prediction ignores vehicles (a test control: mispredicts) */
static vmCvar_t	cg_oaxVehView;	/* 0: by seat, 1: first person, 2: third person */
static vmCvar_t	cg_oaxVehLookReturn;	/* ms without look input before a free look swings back */

/* each type's gun models and sound, registered once a map has the type */
typedef struct {
	qboolean	loaded;
	qhandle_t	gun, barrel, spin, flash;
	sfxHandle_t	fire;
} cgVehAssets_t;

static cgVehAssets_t	vehAssets[OAX_VEH_NUM_TYPES];

/* each vehicle entity's gun as drawn */
typedef struct {
	int			shots;			/* the entity's shot count last seen */
	int			seen;
	int			flashTime;		/* cg.time of the last shot */
	int			locked;			/* the heat's lock bit last seen */
	float		spin;			/* barrel roll, degrees */
} cgVehGun_t;

static cgVehGun_t	vehGun[MAX_GENTITIES];
static int			vehLocksSeen;	/* guns seen going into the overheat lock (tests) */

/* the free look of a seat without the gun: offset from the hull */
static vec3_t	vehLook, vehLastView;
static int		vehLookTime, vehLookVeh = -1, vehLookSeat = -1;

typedef struct {
	int			world;			/* the prediction world, 0 = not made yet */
	int			veh;			/* the predicted vehicle, 0 = none */
	int			ent;			/* its entity number */
	int			type;
	qboolean	active;			/* predicting (drawn from here) this frame */
	int			baseTime;		/* world time of the server state it started from, -1 none */
	int			predTime;		/* world time of pred */
	oaxPhysVehicleState_t pred;
	oaxPhysVehicleInput_t input;
	int			cmdTime;		/* the newest command time */
	/* predicted origins by world time, drawn origins by command time */
	int			ringTime[VP_RING];
	vec3_t		ringOrigin[VP_RING];
	int			dispTime[VP_RING];
	vec3_t		dispOrigin[VP_RING];
	int			dispHead;
	/* error decay */
	vec3_t		errOrigin, errAngles;
	int			errTime;
	/* what is drawn */
	vec3_t		drawOrigin, drawAngles;
	float		drawSusp[OAX_VEH_MAX_WHEELS];
	float		drawSteer, drawSpin;
	float		speed;
	/* statistics */
	int			errN, corrN, snaps, trackN, drivenMs;
	float		errMax, errSum, corrMax, corrSum, trackMax, trackSum;
	int			lastServerTime;	/* world time of the last server state looked at (tracking) */
} cgVehPred_t;

static cgVehPred_t	vp;
static vec3_t		cgVehSavedOrigin;
static int			cgVehSaved;

static void CG_VehRegister( int type ) {
	const bgVehicleType_t *t = BG_VehicleType( type );
	const bgVehicleGun_t *g = BG_VehicleGun( t->gun );
	cgVehAssets_t *a = &vehAssets[type < 0 || type >= OAX_VEH_NUM_TYPES ? 0 : type];

	if ( a->loaded ) {
		return;
	}
	a->loaded = qtrue;
	if ( t->gun == OAX_VGUN_NONE ) {
		return;
	}
	if ( t->gunModel[0] ) {
		a->gun = trap_R_RegisterModel( t->gunModel );
	}
	if ( t->gunBarrelModel[0] ) {
		a->barrel = trap_R_RegisterModel( t->gunBarrelModel );
	}
	if ( t->gun == OAX_VGUN_HMG ) {
		a->spin = trap_R_RegisterModel( "models/weapons/vulcan/vulcan_barrel.md3" );
	}
	a->flash = trap_R_RegisterModel( g->flashModel );
	a->fire = trap_S_RegisterSound( g->fireSound, qfalse );
}

void CG_OAXVehicleInit( void ) {
	trap_Cvar_Register( &cg_oaxVehPredict, "cg_oaxVehPredict", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxVehErrorDecay, "cg_oaxVehErrorDecay", "150", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxVehSnap, "cg_oaxVehSnap", "96", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxVehHud, "cg_oaxVehHud", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxVehClip, "cg_oaxVehClip", "1", CVAR_CHEAT );
	trap_Cvar_Register( &cg_oaxVehView, "cg_oaxVehView", "0", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_oaxVehLookReturn, "cg_oaxVehLookReturn", "800", CVAR_ARCHIVE );
	/* the engine dropped the cgame's worlds with the VM */
	memset( &vp, 0, sizeof( vp ) );
	vp.baseTime = -1;
	vp.lastServerTime = -1;
	cgVehSaved = 0;
	memset( vehAssets, 0, sizeof( vehAssets ) );
	memset( vehGun, 0, sizeof( vehGun ) );
	vehLocksSeen = 0;
	vehLookVeh = vehLookSeat = -1;
	/* the guns of the types this map has (their models are in the
	   configstrings: the spawners register them) */
	{
		int i, k;
		for ( i = 1; i < MAX_MODELS; i++ ) {
			const char *m = CG_ConfigString( CS_MODELS + i );
			if ( !m[0] ) {
				break;
			}
			for ( k = 0; k < OAX_VEH_NUM_TYPES; k++ ) {
				if ( !Q_stricmp( m, bg_vehicleTypes[k].model ) ) {
					CG_VehRegister( k );
				}
			}
		}
	}
}

/* the vehicle entity's origin and angles at cg.time (interpolated) */
static void CG_VehLerpRaw( centity_t *cent, vec3_t origin, vec3_t angles ) {
	float f = 0;
	int i;

	if ( cg.nextSnap && cg.nextSnap->serverTime > cg.snap->serverTime ) {
		f = (float)( cg.time - cg.snap->serverTime ) / ( cg.nextSnap->serverTime - cg.snap->serverTime );
	}
	if ( cent->interpolate && f > 0 ) {
		for ( i = 0; i < 3; i++ ) {
			origin[i] = cent->currentState.pos.trBase[i] + f * ( cent->nextState.pos.trBase[i] - cent->currentState.pos.trBase[i] );
			angles[i] = LerpAngle( cent->currentState.apos.trBase[i], cent->nextState.apos.trBase[i], f );
		}
	} else {
		VectorCopy( cent->currentState.pos.trBase, origin );
		VectorCopy( cent->currentState.apos.trBase, angles );
	}
}

/* as drawn: predicted for the driver's own vehicle, else interpolated */
static void CG_VehLerp( centity_t *cent, vec3_t origin, vec3_t angles ) {
	if ( vp.active && cent->currentState.number == vp.ent ) {
		VectorCopy( vp.drawOrigin, origin );
		VectorCopy( vp.drawAngles, angles );
		return;
	}
	CG_VehLerpRaw( cent, origin, angles );
}

/* the newest server state of an entity: the next snapshot's if there is one */
static const entityState_t *CG_VehNewestState( int num ) {
	snapshot_t *sn = cg.nextSnap ? cg.nextSnap : cg.snap;
	int i;

	if ( sn ) {
		for ( i = 0; i < sn->numEntities; i++ ) {
			if ( sn->entities[i].number == num ) {
				return &sn->entities[i];
			}
		}
	}
	return &cg_entities[num].currentState;
}

/* the local player's seat: vehicle entity number and seat, -1 on foot */
static int CG_VehSeat( int *seat ) {
	playerState_t *ps = &cg.predictedPlayerState;
	int v, num;

	if ( !( ps->pm_flags & PMF_OAX_VEHICLE ) || ps->stats[STAT_OAX_VEHICLE] <= 0 ) {
		return -1;
	}
	v = ps->stats[STAT_OAX_VEHICLE] - 1;
	num = v % 1024;
	*seat = v / 1024;
	if ( num < 0 || num >= MAX_GENTITIES || cg_entities[num].currentState.eType != ET_OAX_VEHICLE ) {
		return -1;
	}
	return num;
}

/* the local player drives */
qboolean CG_OAXVehicleDriving( void ) {
	int seat = 0;
	return CG_VehSeat( &seat ) >= 0 && seat == OAX_VEH_SEAT_DRIVER;
}

/* this seat's view: first person (qtrue) or the chase camera */
static qboolean CG_VehFirstPerson( int type, int seat ) {
	trap_Cvar_Update( &cg_oaxVehView );
	if ( cg_oaxVehView.integer == 1 ) {
		return qtrue;
	}
	if ( cg_oaxVehView.integer == 2 ) {
		return qfalse;
	}
	return seat != OAX_VEH_SEAT_DRIVER || BG_VehicleType( type )->cockpit;
}

/* before the view is built: -1 on foot, else 1 for the chase camera, 0 first person */
int CG_OAXVehicleCamera( void ) {
	int seat = 0, num = CG_VehSeat( &seat );

	if ( num < 0 ) {
		return -1;
	}
	return CG_VehFirstPerson( cg_entities[num].currentState.generic1, seat ) ? 0 : 1;
}

/*
=================
CG_OAXToggleView_f

toggleview: in a vehicle, first person and the chase camera
(cg_oaxVehView); on foot, cg_thirdPerson.
=================
*/
void CG_OAXToggleView_f( void ) {
	int seat = 0, num = CG_VehSeat( &seat );

	if ( num < 0 ) {
		trap_Cvar_Set( "cg_thirdPerson", cg_thirdPerson.integer ? "0" : "1" );
		return;
	}
	trap_Cvar_Set( "cg_oaxVehView", CG_VehFirstPerson( cg_entities[num].currentState.generic1, seat ) ? "2" : "1" );
}

/* a player not to draw: the driver inside a cockpit */
qboolean CG_OAXVehicleHidesPlayer( int clientNum ) {
	int i, seat = 0, num;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cg.snap ) {
		return qfalse;
	}
	if ( clientNum == cg.predictedPlayerState.clientNum ) {
		num = CG_VehSeat( &seat );
		return num >= 0 && seat == OAX_VEH_SEAT_DRIVER && BG_VehicleType( cg_entities[num].currentState.generic1 )->cockpit;
	}
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		const entityState_t *es = &cg.snap->entities[i];
		if ( es->eType == ET_OAX_VEHICLE && es->otherEntityNum == clientNum ) {
			return BG_VehicleType( es->generic1 )->cockpit;
		}
	}
	return qfalse;
}

/*
==============================================================================
own-vehicle prediction
==============================================================================
*/

static void CG_VehQuatToAngles( const float *quat, vec3_t angles ) {
	vec3_t axis[3];
	BG_QuatToAxis( quat, axis );
	BG_VehAxisToAngles( axis, angles );
}

/* a command's time as the server uses it (pmove_fixed rounds it up) */
static int CG_VehCmdTime( const usercmd_t *cmd ) {
	int t = cmd->serverTime;
	if ( pmove_fixed.integer && pmove_msec.integer > 0 ) {
		t = ( ( t + pmove_msec.integer - 1 ) / pmove_msec.integer ) * pmove_msec.integer;
	}
	return t;
}

static int CG_VehWorld( void ) {
	oaxPhysWorldDef_t def;
	oaxPhysShapeDef_t mat;
	int bodies[8];

	if ( vp.world ) {
		return vp.world;
	}
	/* the server's world: G_PhysWorld + G_VehWorld */
	BG_PhysWorldDefInit( &def, cg.snap ? cg.snap->ps.gravity : 800 );
	def.workerCount = 1;
	vp.world = trap_Phys_WorldCreate( &def );
	if ( vp.world ) {
		trap_Phys_WorldAddBSP( vp.world, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, PHYS_BSP_PATCHES, NULL );
		BG_PhysShapeDefInit( &mat, PHYS_SHAPE_BOX, 0 );
		mat.friction = 0.8f;
		trap_Phys_WorldAddTerrain( vp.world, &mat, bodies, 8 );
	}
	return vp.world;
}

static void CG_VehPredDrop( void ) {
	if ( vp.veh ) {
		trap_Phys_VehicleDestroy( vp.veh );
	}
	vp.veh = 0;
	vp.active = qfalse;
	vp.baseTime = -1;
	VectorClear( vp.errOrigin );
	VectorClear( vp.errAngles );
}

/* the input for the tick that starts at t: the newest command at or before it */
static void CG_VehInputAt( int t, int cmdNum ) {
	usercmd_t uc;
	int c;

	for ( c = cmdNum; c > cmdNum - CMD_BACKUP + 1; c-- ) {
		if ( !trap_GetUserCmd( c, &uc ) ) {
			break;
		}
		if ( CG_VehCmdTime( &uc ) <= t ) {
			BG_VehCmdToInput( &uc, &vp.input );
			return;
		}
	}
}

/* step the prediction to target; returns qtrue (and the origin then) if it
   passed through world time `at` */
static qboolean CG_VehReplay( int target, int cmdNum, int at, vec3_t atOrigin, vec3_t atAngles ) {
	qboolean found = qfalse;
	int n = 0, k;

	if ( vp.predTime == at ) {
		VectorCopy( vp.pred.origin, atOrigin );
		CG_VehQuatToAngles( vp.pred.quat, atAngles );
		found = qtrue;
	}
	while ( vp.predTime + OAX_VEH_TICK_MSEC <= target && n < VP_MAX_REPLAY ) {
		CG_VehInputAt( vp.predTime, cmdNum );
		trap_Phys_VehicleSetInput( vp.veh, &vp.input );
		trap_Phys_WorldStep( vp.world, -1 );
		vp.predTime += OAX_VEH_TICK_MSEC;
		trap_Phys_VehicleGetState( vp.veh, &vp.pred );
		k = ( vp.predTime / OAX_VEH_TICK_MSEC ) & ( VP_RING - 1 );
		vp.ringTime[k] = vp.predTime;
		VectorCopy( vp.pred.origin, vp.ringOrigin[k] );
		if ( vp.predTime == at ) {
			VectorCopy( vp.pred.origin, atOrigin );
			CG_VehQuatToAngles( vp.pred.quat, atAngles );
			found = qtrue;
		}
		n++;
	}
	return found;
}

/* how far what was drawn at command time t was from the server's vehicle at t */
static void CG_VehTrack( const oaxPhysVehicleState_t *server, int t ) {
	int i, best = -1, bestDt = 1 << 30;
	vec3_t at;
	float d;

	for ( i = 0; i < VP_RING; i++ ) {
		int dt = t - vp.dispTime[i];
		if ( vp.dispTime[i] && dt >= 0 && dt < bestDt ) {
			best = i;
			bestDt = dt;
		}
	}
	if ( best < 0 || bestDt >= OAX_VEH_TICK_MSEC ) {
		return;
	}
	/* the drawn position carried to t at the server's velocity */
	VectorMA( vp.dispOrigin[best], bestDt * 0.001f, server->velocity, at );
	d = Distance( at, server->origin );
	vp.trackN++;
	vp.trackSum += d;
	if ( d > vp.trackMax ) {
		vp.trackMax = d;
	}
}

static void CG_VehDrawn( int cmdTime, const vec3_t origin ) {
	vp.dispHead = ( vp.dispHead + 1 ) & ( VP_RING - 1 );
	vp.dispTime[vp.dispHead] = cmdTime;
	VectorCopy( origin, vp.dispOrigin[vp.dispHead] );
}

static void CG_VehPublish( void ) {
	float secs = vp.drivenMs * 0.001f;

	BG_OAXDebugSetInt( "cg_vehpred_on", vp.active );
	BG_OAXDebugSetInt( "cg_vehpred_err_n", vp.errN );
	BG_OAXDebugSet( "cg_vehpred_err_max", va( "%.3f", vp.errMax ) );
	BG_OAXDebugSet( "cg_vehpred_err_mean", va( "%.3f", vp.errN ? vp.errSum / vp.errN : 0 ) );
	BG_OAXDebugSetInt( "cg_vehpred_corr_n", vp.corrN );
	BG_OAXDebugSet( "cg_vehpred_corr_per_s", va( "%.2f", secs > 0 ? vp.corrN / secs : 0 ) );
	BG_OAXDebugSet( "cg_vehpred_corr_max", va( "%.3f", vp.corrMax ) );
	BG_OAXDebugSet( "cg_vehpred_corr_mean", va( "%.3f", vp.corrN ? vp.corrSum / vp.corrN : 0 ) );
	BG_OAXDebugSetInt( "cg_vehpred_snaps", vp.snaps );
	BG_OAXDebugSetInt( "cg_vehpred_track_n", vp.trackN );
	BG_OAXDebugSet( "cg_vehpred_track_max", va( "%.3f", vp.trackMax ) );
	BG_OAXDebugSet( "cg_vehpred_track_mean", va( "%.3f", vp.trackN ? vp.trackSum / vp.trackN : 0 ) );
	BG_OAXDebugSetInt( "cg_vehpred_driven_ms", vp.drivenMs );
	BG_OAXDebugSet( "cg_vehpred_pos", va( "%.2f %.2f %.2f %i %i", vp.drawOrigin[0], vp.drawOrigin[1], vp.drawOrigin[2], vp.predTime, vp.cmdTime ) );
}

/* every frame, before the view: the driver's own vehicle */
static void CG_VehPredict( void ) {
	int seat = 0, num = CG_VehSeat( &seat ), cmdNum, worldTime, flags, i;
	const entityState_t *es;
	oaxPhysVehicleState_t server;
	usercmd_t cmd;
	centity_t *cent;
	float f;

	trap_Cvar_Update( &cg_oaxVehPredict );
	trap_Cvar_Update( &cg_oaxVehErrorDecay );
	trap_Cvar_Update( &cg_oaxVehSnap );
	vp.active = qfalse;
	if ( num < 0 || seat != OAX_VEH_SEAT_DRIVER ) {
		if ( vp.veh ) {
			CG_VehPredDrop();
		}
		return;
	}
	cent = &cg_entities[num];
	es = CG_VehNewestState( num );
	BG_VehStateFromEntity( es, &server, &worldTime, &flags );
	cmdNum = trap_GetCurrentCmdNumber();
	trap_GetUserCmd( cmdNum, &cmd );
	vp.cmdTime = CG_VehCmdTime( &cmd );
	vp.drivenMs += cg.frametime;

	/* a new server state: how far the drawn vehicle was from it */
	if ( worldTime != vp.lastServerTime ) {
		vp.lastServerTime = worldTime;
		CG_VehTrack( &server, worldTime );
	}

	if ( !cg_oaxVehPredict.integer || !BG_OAXFeature( "physics_vehicle_state" ) || !CG_VehWorld() ) {
		vec3_t o, a;
		if ( vp.veh ) {
			CG_VehPredDrop();
		}
		CG_VehLerpRaw( cent, o, a );
		CG_VehDrawn( vp.cmdTime, o );
		VectorCopy( o, vp.drawOrigin );
		CG_VehPublish();
		return;
	}

	if ( vp.veh && ( vp.ent != num || vp.type != es->generic1 ) ) {
		CG_VehPredDrop();
	}
	if ( !vp.veh ) {
		oaxPhysVehicleDef_t def;
		BG_VehicleDef( es->generic1, server.origin, server.quat, &def );
		def.userData = num;
		vp.veh = trap_Phys_VehicleCreate( vp.world, &def );
		if ( !vp.veh ) {
			return;
		}
		vp.ent = num;
		vp.type = es->generic1;
		vp.baseTime = -1;
		memset( vp.ringTime, 0, sizeof( vp.ringTime ) );
	}

	if ( worldTime != vp.baseTime ) {
		vec3_t oldOrigin, oldAngles, newOrigin, newAngles, delta;
		int oldTime = vp.predTime, k;
		qboolean hadOld = vp.baseTime >= 0, found;

		/* how wrong the prediction for this time was */
		k = ( worldTime / OAX_VEH_TICK_MSEC ) & ( VP_RING - 1 );
		if ( hadOld && vp.ringTime[k] == worldTime ) {
			float e = Distance( vp.ringOrigin[k], server.origin );
			vp.errN++;
			vp.errSum += e;
			if ( e > vp.errMax ) {
				vp.errMax = e;
			}
		}
		VectorCopy( vp.pred.origin, oldOrigin );
		CG_VehQuatToAngles( vp.pred.quat, oldAngles );
		/* restart from the server's state and replay the commands since */
		trap_Phys_VehicleSetState( vp.veh, &server, ( flags & OAX_VEHS_AWAKE ) ? PHYS_VSS_AWAKE : 0 );
		vp.pred = server;
		vp.predTime = worldTime;
		vp.baseTime = worldTime;
		found = CG_VehReplay( vp.cmdTime, cmdNum, oldTime, newOrigin, newAngles );
		if ( hadOld && found ) {
			float d;
			VectorSubtract( oldOrigin, newOrigin, delta );
			d = VectorLength( delta );
			if ( d > 0.01f ) {
				vp.corrN++;
				vp.corrSum += d;
				if ( d > vp.corrMax ) {
					vp.corrMax = d;
				}
			}
			if ( d > cg_oaxVehSnap.value ) {
				/* a teleport: no smoothing */
				vp.snaps++;
				VectorClear( vp.errOrigin );
				VectorClear( vp.errAngles );
			} else {
				f = cg_oaxVehErrorDecay.value > 0 ? 1.0f - (float)( cg.time - vp.errTime ) / cg_oaxVehErrorDecay.value : 0;
				if ( f < 0 ) {
					f = 0;
				}
				VectorScale( vp.errOrigin, f, vp.errOrigin );
				VectorScale( vp.errAngles, f, vp.errAngles );
				VectorAdd( vp.errOrigin, delta, vp.errOrigin );
				for ( i = 0; i < 3; i++ ) {
					vp.errAngles[i] += AngleSubtract( oldAngles[i], newAngles[i] );
				}
				vp.errTime = cg.time;
			}
		}
	} else {
		vec3_t o, a;
		CG_VehReplay( vp.cmdTime, cmdNum, -1, o, a );
	}

	/* drawn at the newest command time, the correction decaying away */
	f = cg_oaxVehErrorDecay.value > 0 ? 1.0f - (float)( cg.time - vp.errTime ) / cg_oaxVehErrorDecay.value : 0;
	if ( f < 0 ) {
		f = 0;
	}
	VectorMA( vp.pred.origin, ( vp.cmdTime - vp.predTime ) * 0.001f, vp.pred.velocity, vp.drawOrigin );
	VectorMA( vp.drawOrigin, f, vp.errOrigin, vp.drawOrigin );
	CG_VehQuatToAngles( vp.pred.quat, vp.drawAngles );
	VectorMA( vp.drawAngles, f, vp.errAngles, vp.drawAngles );
	for ( i = 0; i < OAX_VEH_MAX_WHEELS; i++ ) {
		vp.drawSusp[i] = vp.pred.suspension[i];
	}
	vp.drawSteer = RAD2DEG( vp.pred.steer );
	vp.drawSpin = AngleMod( RAD2DEG( vp.pred.spin[0] ) );
	vp.speed = vp.pred.speed;
	vp.active = qtrue;
	CG_VehDrawn( vp.cmdTime, vp.drawOrigin );
	CG_VehPublish();
}

/*
==============================================================================
drawing
==============================================================================
*/

/*
CG_VehGroundFx (the map's ground effects, cg_oax_fx.c): each wheel on the
ground leaves a tyre track and, at speed, a puff of dust; a wheel in water
leaves rings and spray instead. A hover vehicle's thrusters raise dust or
rings from what is under them, more the lower they ride.
*/
#define VEH_FX_MSEC	70
#define VEH_FX_RANGE	3000	/* farther vehicles kick up nothing anyone would see */
static int vehFxTime[MAX_GENTITIES];

static void CG_VehGroundFx( centity_t *cent, refEntity_t *ent, const bgVehicleType_t *t, const float *susp ) {
	const entityState_t *s = &cent->currentState;
	float speed = VectorLength( s->pos.trDelta );
	qboolean puff;
	vec3_t up = { 0, 0, 1 };
	int i;

	if ( !CG_OAXGroundFxOn() || Distance( ent->origin, cg.refdef.vieworg ) > VEH_FX_RANGE ) {
		return;
	}
	puff = cg.time - vehFxTime[s->number] >= VEH_FX_MSEC || cg.time < vehFxTime[s->number];
	if ( puff ) {
		vehFxTime[s->number] = cg.time;
	}
	for ( i = 0; i < t->numWheels && i < OAX_VEH_MAX_WHEELS; i++ ) {
		vec3_t local, hub, contact, end, surface;
		trace_t tr;

		VectorCopy( t->wheels[i], local );
		if ( t->physType == 1 ) {
			/* a thruster: what is under it, within reach; idling, one
			   thruster at a time stirs the ground */
			if ( puff && speed < 60 && i != ( cg.time / VEH_FX_MSEC ) % t->numWheels ) {
				continue;
			}
			BG_VehLocalToWorld( ent->origin, ent->axis, local, hub );
			VectorCopy( hub, end );
			end[2] -= 110;
			if ( CG_OAXWaterSurface( hub, 0, 110, surface ) ) {
				if ( puff ) {
					CG_OAXRipple( surface, 1.4f );
					if ( speed > 120 ) {
						CG_OAXSplash( surface, 0.8f );
					}
				}
				continue;
			}
			CG_Trace( &tr, hub, NULL, NULL, end, s->number, MASK_SOLID );
			if ( puff && tr.fraction < 1.0f && !tr.startsolid ) {
				CG_OAXGroundDust( tr.endpos, tr.plane.normal, 0.6f + ( 1.0f - tr.fraction ) * 0.8f + speed * 0.0008f );
			}
			continue;
		}
		if ( !( s->legsAnim & ( 1 << i ) ) && !( i >= 4 && ( s->legsAnim & 15 ) ) ) {
			continue;	/* off the ground (the middle pair rides with the others) */
		}
		local[2] -= susp[i] + t->wheelRadius;
		BG_VehLocalToWorld( ent->origin, ent->axis, local, contact );
		VectorCopy( contact, hub );
		hub[2] += t->wheelRadius;
		if ( CG_OAXWaterSurface( hub, t->wheelRadius, t->wheelRadius + 4, surface ) ) {
			if ( puff && speed > 40 ) {
				CG_OAXRipple( surface, 1.0f + speed * 0.001f );
				if ( speed > 200 ) {
					CG_OAXSplash( surface, 0.5f + speed * 0.0008f );
				}
			}
			continue;
		}
		VectorCopy( contact, end );
		end[2] -= 16;
		CG_Trace( &tr, hub, NULL, NULL, end, s->number, MASK_SOLID );
		if ( tr.fraction >= 1.0f || tr.startsolid ) {
			continue;
		}
		CG_OAXTrack( s->number, i, tr.endpos, tr.plane.normal, t->wheelRadius * 1.3f );
		if ( puff && speed > 150 && ( t->wheels[i][0] < 0 || speed > 450 ) ) {
			CG_OAXGroundDust( tr.endpos, up, 0.5f + speed * 0.0012f );
		}
	}
}

/* the view angles that aim a vehicle's gun: its user's (the local
   player's own as predicted), NULL when nobody is on the gun seat */
static const float *CG_VehGunUserAngles( const entityState_t *s ) {
	int seat = BG_VehGunSeat( s->generic1 ), user;

	if ( seat < 0 ) {
		return NULL;
	}
	user = seat == OAX_VEH_SEAT_DRIVER ? s->otherEntityNum : s->otherEntityNum2;
	if ( user < 0 || user >= MAX_CLIENTS ) {
		return NULL;
	}
	if ( user == cg.predictedPlayerState.clientNum ) {
		return cg.predictedPlayerState.viewangles;
	}
	if ( !cg_entities[user].currentValid ) {
		return NULL;
	}
	return cg_entities[user].lerpAngles;
}

/* the gun's aim on a hull as drawn; resting straight ahead without a user */
static qboolean CG_VehGunPose( const entityState_t *s, const vec3_t origin, vec3_t axis[3], float *yaw, float *pitch, vec3_t muzzle, vec3_t dir ) {
	const float *view = CG_VehGunUserAngles( s );
	vec3_t rest;

	if ( !view ) {
		vectoangles( axis[0], rest );
		view = rest;
	}
	return BG_VehGunAim( s->generic1, origin, axis, view, yaw, pitch, muzzle, dir );
}

static void CG_VehScaleAxis( refEntity_t *e, float scale ) {
	if ( scale != 1.0f ) {
		VectorScale( e->axis[0], scale, e->axis[0] );
		VectorScale( e->axis[1], scale, e->axis[1] );
		VectorScale( e->axis[2], scale, e->axis[2] );
		e->nonNormalizedAxes = qtrue;
	}
}

/* the mounted gun: the model at its mount turned to the aim, the muzzle
   flash and the shot's sound when the shot count moves */
static void CG_VehGun( centity_t *cent, const refEntity_t *hull, const bgVehicleType_t *t ) {
	entityState_t *s = &cent->currentState;
	const bgVehicleGun_t *g = BG_VehicleGun( t->gun );
	cgVehAssets_t *a;
	cgVehGun_t *vg = &vehGun[s->number];
	refEntity_t gun, part, flash;
	vec3_t muzzle, dir, local, ang, axis[3];
	float yaw, pitch;
	qboolean fired = qfalse;

	if ( t->gun == OAX_VGUN_NONE || !CG_VehGunPose( s, hull->origin, (vec3_t *)hull->axis, &yaw, &pitch, muzzle, dir ) ) {
		return;
	}
	CG_VehRegister( s->generic1 );
	a = &vehAssets[s->generic1];

	/* a new shot (not on first sight: a count that was already there) */
	if ( !vg->seen || cg.time < vg->flashTime ) {
		vg->seen = 1;
		vg->shots = s->powerups;
		vg->flashTime = -100000;
	} else if ( s->powerups != vg->shots ) {
		vg->shots = s->powerups;
		vg->flashTime = cg.time;
		fired = qtrue;
	}
	if ( ( s->clientNum & OAX_VEH_HEAT_LOCK ) && !vg->locked ) {
		vehLocksSeen++;
		BG_OAXDebugSetInt( "cg_veh_gun_locks", vehLocksSeen );
	}
	vg->locked = ( s->clientNum & OAX_VEH_HEAT_LOCK ) != 0;

	memset( &gun, 0, sizeof( gun ) );
	BG_VehLocalToWorld( hull->origin, (vec3_t *)hull->axis, t->gunMount, gun.origin );
	VectorCopy( gun.origin, gun.oldorigin );
	VectorCopy( hull->origin, gun.lightingOrigin );
	/* with a barrel model the gun only turns; the barrel pitches */
	ang[PITCH] = a->barrel ? 0 : -pitch;
	ang[YAW] = yaw;
	ang[ROLL] = 0;
	AnglesToAxis( ang, axis );
	MatrixMultiply( axis, (vec3_t *)hull->axis, gun.axis );
	gun.renderfx = RF_MINLIGHT | RF_LIGHTING_ORIGIN;
	if ( a->gun ) {
		gun.hModel = a->gun;
		BG_VehLocalToWorld( gun.origin, gun.axis, t->gunModelOffset, gun.origin );
		VectorCopy( gun.origin, gun.oldorigin );
		CG_VehScaleAxis( &gun, t->gunScale );
		trap_R_AddRefEntityToScene( &gun );
	}
	if ( a->barrel ) {
		memset( &part, 0, sizeof( part ) );
		local[0] = t->gunBarrelPivot[0];
		local[1] = t->gunBarrelPivot[1];
		local[2] = t->gunBarrelPivot[2];
		BG_VehLocalToWorld( gun.origin, gun.axis, local, part.origin );
		VectorCopy( part.origin, part.oldorigin );
		VectorCopy( hull->origin, part.lightingOrigin );
		ang[PITCH] = -pitch;
		ang[YAW] = 0;
		AnglesToAxis( ang, axis );
		MatrixMultiply( axis, gun.axis, part.axis );
		part.hModel = a->barrel;
		part.renderfx = RF_MINLIGHT | RF_LIGHTING_ORIGIN;
		trap_R_AddRefEntityToScene( &part );
	}
	if ( a->spin && a->gun ) {
		/* the heavy machine gun's barrels spin while it fires */
		if ( cg.time - vg->flashTime < 300 ) {
			vg->spin = AngleMod( vg->spin + cg.frametime * 1.2f );
		}
		memset( &part, 0, sizeof( part ) );
		ang[PITCH] = 0;
		ang[YAW] = 0;
		ang[ROLL] = vg->spin;
		AnglesToAxis( ang, part.axis );
		part.hModel = a->spin;
		part.renderfx = RF_MINLIGHT | RF_LIGHTING_ORIGIN;
		VectorCopy( hull->origin, part.lightingOrigin );
		CG_PositionRotatedEntityOnTag( &part, &gun, a->gun, "tag_barrel" );
		part.nonNormalizedAxes = gun.nonNormalizedAxes;
		trap_R_AddRefEntityToScene( &part );
	}

	/* the flash: at the muzzle, along the shot (the plasma guns alternate) */
	if ( t->gun == OAX_VGUN_PLASMA ) {
		VectorMA( muzzle, ( ( vg->shots - 1 ) & 1 ) ? -14.0f : 14.0f, hull->axis[1], muzzle );
	}
	if ( fired && a->fire ) {
		trap_S_StartSound( muzzle, ENTITYNUM_WORLD, CHAN_AUTO, a->fire );
	}
	if ( cg.time - vg->flashTime < 60 && a->flash ) {
		memset( &flash, 0, sizeof( flash ) );
		if ( a->spin && a->gun ) {
			AxisClear( flash.axis );
			CG_PositionRotatedEntityOnTag( &flash, &gun, a->gun, "tag_flash" );
			flash.nonNormalizedAxes = gun.nonNormalizedAxes;
		} else {
			vectoangles( dir, ang );
			ang[ROLL] = crandom() * 20;
			AnglesToAxis( ang, flash.axis );
			VectorCopy( muzzle, flash.origin );
			CG_VehScaleAxis( &flash, t->gun == OAX_VGUN_CANNON ? 2.5f : 1.2f );
		}
		VectorCopy( flash.origin, flash.oldorigin );
		flash.hModel = a->flash;
		trap_R_AddRefEntityToScene( &flash );
		trap_R_AddLightToScene( muzzle, t->gun == OAX_VGUN_CANNON ? 300 : 180, g->flashColor[0], g->flashColor[1], g->flashColor[2] );
	}
}

void CG_OAXVehicle( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	const bgVehicleType_t *t = BG_VehicleType( s->generic1 );
	refEntity_t ent;
	float susp[OAX_VEH_MAX_WHEELS], steer, spin, f = cg.frameInterpolation;
	int i;

	memset( &ent, 0, sizeof( ent ) );
	if ( vp.active && s->number == vp.ent ) {
		/* the driver's own vehicle: predicted */
		VectorCopy( vp.drawOrigin, cent->lerpOrigin );
		VectorCopy( vp.drawAngles, cent->lerpAngles );
	}
	VectorCopy( cent->lerpOrigin, ent.origin );
	VectorCopy( ent.origin, ent.oldorigin );
	AnglesToAxis( cent->lerpAngles, ent.axis );
	ent.hModel = cgs.gameModels[s->modelindex];
	ent.renderfx = RF_MINLIGHT;
	trap_R_AddRefEntityToScene( &ent );

	/* wheels */
	if ( vp.active && s->number == vp.ent ) {
		for ( i = 0; i < OAX_VEH_MAX_WHEELS; i++ ) {
			susp[i] = vp.drawSusp[i];
		}
		steer = vp.drawSteer;
		spin = vp.drawSpin;
	} else {
		susp[0] = s->origin2[0];
		susp[1] = s->origin2[1];
		susp[2] = s->origin2[2];
		susp[3] = s->angles2[0];
		steer = s->angles2[1];
		spin = s->angles2[2];
		if ( cent->interpolate ) {
			entityState_t *n = &cent->nextState;
			susp[0] += f * ( n->origin2[0] - susp[0] );
			susp[1] += f * ( n->origin2[1] - susp[1] );
			susp[2] += f * ( n->origin2[2] - susp[2] );
			susp[3] += f * ( n->angles2[0] - susp[3] );
			steer += f * ( n->angles2[1] - steer );
			spin = LerpAngle( spin, n->angles2[2], f );
		}
		/* six wheels: the middle pair is not sent; drawn between front and back */
		susp[4] = 0.5f * ( susp[0] + susp[2] );
		susp[5] = 0.5f * ( susp[1] + susp[3] );
	}
	if ( s->modelindex2 && t->numWheels ) {
		for ( i = 0; i < t->numWheels && i < OAX_VEH_MAX_WHEELS; i++ ) {
			refEntity_t w;
			vec3_t local, ang, waxis[3];
			memset( &w, 0, sizeof( w ) );
			VectorCopy( t->wheels[i], local );
			local[2] -= susp[i];
			BG_VehLocalToWorld( ent.origin, ent.axis, local, w.origin );
			VectorCopy( w.origin, w.oldorigin );
			ang[PITCH] = spin;
			ang[YAW] = i < 2 ? steer : ( t->rearSteer && i < 4 ? -steer : 0 );
			/* one wheel model for both sides: the left ones turned round so
			   the hub faces out (and rolling the other way about their axis) */
			if ( t->mirrorLeftWheels && t->wheels[i][1] > 0 ) {
				ang[YAW] += 180;
				ang[PITCH] = -spin;
			}
			ang[ROLL] = 0;
			AnglesToAxis( ang, waxis );
			MatrixMultiply( waxis, ent.axis, w.axis );
			w.hModel = cgs.gameModels[s->modelindex2];
			w.renderfx = RF_MINLIGHT;
			trap_R_AddRefEntityToScene( &w );
		}
	}

	/* hover: a glow under each thruster */
	if ( t->physType == 1 ) {
		for ( i = 0; i < t->numWheels; i++ ) {
			vec3_t p;
			BG_VehLocalToWorld( ent.origin, ent.axis, t->wheels[i], p );
			p[2] -= 6;
			trap_R_AddLightToScene( p, 70, 0.3f, 0.6f, 1.0f );
		}
	}

	CG_VehGroundFx( cent, &ent, t, susp );
	CG_VehGun( cent, &ent, t );

	/* damaged: smoke */
	if ( s->frame < 40 && ( cg.time / 120 ) != ( ( cg.time - cg.frametime ) / 120 ) ) {
		vec3_t p, up = { 0, 0, 40 };
		VectorCopy( ent.origin, p );
		p[2] += t->halfExtents[2];
		CG_SmokePuff( p, up, 20, 0.3f, 0.3f, 0.3f, 0.5f, 900, cg.time, 0, 0, cgs.media.smokePuffShader );
	}
}

/* a seat without the gun: the view's turns since last frame move a free
   look relative to the hull, which swings back once the stick rests */
static void CG_VehFreeLook( int num, int seat, float yawLimit ) {
	const float *v = cg.predictedPlayerState.viewangles;
	float k;

	if ( num != vehLookVeh || seat != vehLookSeat ) {
		VectorClear( vehLook );
		vehLookVeh = num;
		vehLookSeat = seat;
		vehLookTime = 0;
	} else {
		float dy = AngleSubtract( v[YAW], vehLastView[YAW] ), dp = AngleSubtract( v[PITCH], vehLastView[PITCH] );
		if ( fabs( dy ) + fabs( dp ) > 0.01f ) {
			vehLookTime = cg.time;
		}
		vehLook[YAW] += dy;
		vehLook[PITCH] += dp;
	}
	VectorCopy( v, vehLastView );
	if ( vehLook[YAW] > yawLimit ) {
		vehLook[YAW] = yawLimit;
	} else if ( vehLook[YAW] < -yawLimit ) {
		vehLook[YAW] = -yawLimit;
	}
	if ( vehLook[PITCH] > 60 ) {
		vehLook[PITCH] = 60;
	} else if ( vehLook[PITCH] < -60 ) {
		vehLook[PITCH] = -60;
	}
	trap_Cvar_Update( &cg_oaxVehLookReturn );
	if ( cg.time - vehLookTime > cg_oaxVehLookReturn.integer ) {
		k = 1.0f - cg.frametime * 0.004f;
		if ( k < 0 ) {
			k = 0;
		}
		vehLook[YAW] *= k;
		vehLook[PITCH] *= k;
	}
}

/* the hull's axis turned by the free look */
static void CG_VehLookAxis( vec3_t hull[3], vec3_t out[3] ) {
	vec3_t la[3];

	AnglesToAxis( vehLook, la );
	MatrixMultiply( la, hull, out );
}

/* after CG_CalcViewValues: the prediction, then the view from the seat */
void CG_OAXVehicleView( void ) {
	int seat = 0, num, type;
	const bgVehicleType_t *t;
	centity_t *cent;
	vec3_t org, angles, axis[3], seatOrg, view;
	qboolean gunUser, firstPerson;

	CG_VehPredict();
	num = CG_VehSeat( &seat );
	cgVehSaved = 0;
	if ( num < 0 ) {
		vehLookVeh = -1;
		return;
	}
	cent = &cg_entities[num];
	type = cent->currentState.generic1;
	t = BG_VehicleType( type );
	CG_VehLerp( cent, org, angles );
	AnglesToAxis( angles, axis );
	BG_VehLocalToWorld( org, axis, t->seats[seat], seatOrg );
	gunUser = BG_VehGunSeat( type ) == seat;
	firstPerson = CG_VehFirstPerson( type, seat );
	BG_OAXDebugSet( "cg_veh_view", va( "%i %i %i", seat, firstPerson, gunUser ) );

	/* the local occupant drawn on the seat as drawn; put back after the
	   frame so prediction never sees it */
	VectorCopy( cg.predictedPlayerState.origin, cgVehSavedOrigin );
	cgVehSaved = 1;
	VectorCopy( seatOrg, cg.predictedPlayerState.origin );

	/* a seat without the gun looks relative to the hull; the gun's user
	   (and a gunner without one) looks with the view angles themselves */
	if ( !gunUser && seat == OAX_VEH_SEAT_DRIVER ) {
		CG_VehFreeLook( num, seat, firstPerson ? 110 : 170 );
	} else {
		vehLookVeh = -1;
	}

	if ( firstPerson ) {
		if ( seat == OAX_VEH_SEAT_DRIVER && t->cockpit ) {
			if ( gunUser ) {
				/* sighting from the turret: the eye turns with the gun */
				vec3_t muzzle, dir, mount, ga[3], ya[3], yang = { 0, 0, 0 };
				float yaw = 0, pitch = 0;
				BG_VehGunAim( type, org, axis, cg.predictedPlayerState.viewangles, &yaw, &pitch, muzzle, dir );
				yang[YAW] = yaw;
				AnglesToAxis( yang, ya );
				MatrixMultiply( ya, axis, ga );
				BG_VehLocalToWorld( org, axis, t->gunMount, mount );
				BG_VehLocalToWorld( mount, ga, t->eye, cg.refdef.vieworg );
				VectorCopy( cg.predictedPlayerState.viewangles, cg.refdefViewAngles );
			} else {
				vec3_t va[3];
				BG_VehLocalToWorld( org, axis, t->eye, cg.refdef.vieworg );
				CG_VehLookAxis( axis, va );
				BG_VehAxisToAngles( va, cg.refdefViewAngles );
			}
		} else {
			VectorCopy( seatOrg, cg.refdef.vieworg );
			cg.refdef.vieworg[2] += cg.predictedPlayerState.viewheight;
			if ( !gunUser && seat == OAX_VEH_SEAT_DRIVER ) {
				vec3_t va[3];
				CG_VehLookAxis( axis, va );
				BG_VehAxisToAngles( va, cg.refdefViewAngles );
			}
		}
	} else {
		/* the chase camera: orbiting with the aim, or behind the vehicle
		   turned by the free look */
		vec3_t focus, fwd, cam;
		vec3_t mins = { -8, -8, -8 }, maxs = { 8, 8, 8 };
		trace_t tr;
		if ( !gunUser && seat == OAX_VEH_SEAT_DRIVER ) {
			view[PITCH] = vehLook[PITCH];
			view[YAW] = angles[YAW] + vehLook[YAW];
			view[ROLL] = 0;
		} else {
			VectorCopy( cg.predictedPlayerState.viewangles, view );
		}
		if ( view[PITCH] < -10 ) {
			view[PITCH] = -10;
		}
		view[PITCH] += 12;
		AngleVectors( view, fwd, NULL, NULL );
		VectorCopy( org, focus );
		focus[2] += t->halfExtents[2] + 36;
		VectorMA( focus, -( t->halfExtents[0] * 2 + 140 ), fwd, cam );
		CG_Trace( &tr, focus, mins, maxs, cam, cg.predictedPlayerState.clientNum, MASK_SOLID );
		VectorCopy( tr.endpos, cg.refdef.vieworg );
		/* the gun's user looks where it aims (the pitch the camera was lifted
		   by stays out of the aim) */
		VectorCopy( view, cg.refdefViewAngles );
	}
	AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );
}

/* end of the scene: the predicted origin back as prediction left it */
void CG_OAXVehicleFrameEnd( void ) {
	if ( cgVehSaved ) {
		VectorCopy( cgVehSavedOrigin, cg.predictedPlayerState.origin );
		cgVehSaved = 0;
	}
}

/*
==============================================================================
collision (cg_predict.c): the oriented box the server collides players
with, as of the snapshot the player's prediction runs against
==============================================================================
*/

/* the state collision uses: the next snapshot's at cg.physicsTime */
static const entityState_t *CG_VehClipState( centity_t *cent ) {
	if ( cent->interpolate && cg.nextSnap && cg.physicsTime == cg.nextSnap->serverTime ) {
		return &cent->nextState;
	}
	return &cent->currentState;
}

/* 1: collide with this oriented box; 0: not a vehicle (the stock clip);
   -1: skip it (cg_oaxVehClip 0) */
int CG_OAXVehicleClipModel( centity_t *cent, clipHandle_t *cmodel, vec3_t origin ) {
	const entityState_t *es;
	oaxPhysVehicleState_t st;
	vec3_t axis[3];
	float obb[15];

	if ( cent->currentState.eType != ET_OAX_VEHICLE || !BG_OAXFeature( "ent_obb" ) ) {
		return 0;
	}
	trap_Cvar_Update( &cg_oaxVehClip );
	if ( !cg_oaxVehClip.integer ) {
		return -1;
	}
	es = CG_VehClipState( cent );
	BG_VehStateFromEntity( es, &st, NULL, NULL );
	BG_QuatToAxis( st.quat, axis );
	BG_VehOBB( es->generic1, axis, obb );
	*cmodel = trap_OAX_CM_TempOBBModel( obb, CONTENTS_BODY );
	VectorCopy( es->pos.trBase, origin );
	return 1;
}

/* the vehicle's pose at a time: the drawn pose for the driver's own, else
   between the snapshots (the collision state at cg.physicsTime) */
static void CG_VehPoseAt( centity_t *cent, int time, vec3_t origin, vec3_t axis[3] ) {
	vec3_t angles;
	float f = 0;
	int i;

	if ( vp.active && cent->currentState.number == vp.ent ) {
		VectorCopy( vp.drawOrigin, origin );
		AnglesToAxis( vp.drawAngles, axis );
		return;
	}
	if ( cent->interpolate && cg.nextSnap && cg.nextSnap->serverTime > cg.snap->serverTime ) {
		f = (float)( time - cg.snap->serverTime ) / ( cg.nextSnap->serverTime - cg.snap->serverTime );
		if ( f < 0 ) {
			f = 0;
		} else if ( f > 1 ) {
			f = 1;
		}
	}
	for ( i = 0; i < 3; i++ ) {
		if ( f > 0 ) {
			origin[i] = cent->currentState.pos.trBase[i] + f * ( cent->nextState.pos.trBase[i] - cent->currentState.pos.trBase[i] );
			angles[i] = LerpAngle( cent->currentState.apos.trBase[i], cent->nextState.apos.trBase[i], f );
		} else {
			origin[i] = cent->currentState.pos.trBase[i];
			angles[i] = cent->currentState.apos.trBase[i];
		}
	}
	AnglesToAxis( angles, axis );
}

/* CG_AdjustPositionForMover for a player standing on a vehicle: carried from
   the vehicle's pose at fromTime to its pose at toTime */
qboolean CG_OAXVehicleRider( int num, int fromTime, int toTime, const vec3_t in, vec3_t out ) {
	centity_t *cent;
	vec3_t o0, o1, a0[3], a1[3], d, local;
	int k;

	if ( num <= 0 || num >= ENTITYNUM_MAX_NORMAL ) {
		return qfalse;
	}
	cent = &cg_entities[num];
	if ( cent->currentState.eType != ET_OAX_VEHICLE ) {
		return qfalse;
	}
	CG_VehPoseAt( cent, fromTime, o0, a0 );
	CG_VehPoseAt( cent, toTime, o1, a1 );
	VectorSubtract( in, o0, d );
	for ( k = 0; k < 3; k++ ) {
		local[k] = DotProduct( d, a0[k] );
	}
	BG_VehLocalToWorld( o1, a1, local, out );
	return qtrue;
}

/*
==============================================================================
the HUD: the vehicle's health for its riders, and the speed for the driver
==============================================================================
*/

void CG_OAXVehicleHUD( void ) {
	int seat = 0, num = CG_VehSeat( &seat ), health, speed = 0;
	const bgVehicleType_t *t;
	vec4_t back = { 0, 0, 0, 0.55f }, frame = { 1, 1, 1, 0.8f }, bar, text = { 1, 1, 1, 1 };
	float x = 232, y = 400, w = 176, h = 32, fill;
	char *label;

	trap_Cvar_Update( &cg_oaxVehHud );
	if ( num < 0 || !cg_oaxVehHud.integer ) {
		return;
	}
	t = BG_VehicleType( cg_entities[num].currentState.generic1 );
	health = cg_entities[num].currentState.frame;	/* percent, the server's G_VehUpdateEntity */
	if ( health < 0 ) {
		health = 0;
	} else if ( health > 100 ) {
		health = 100;
	}
	fill = health / 100.0f;
	bar[0] = fill < 0.5f ? 1.0f : 2.0f * ( 1.0f - fill );
	bar[1] = fill > 0.5f ? 1.0f : 2.0f * fill;
	bar[2] = 0.1f;
	bar[3] = 0.9f;

	CG_FillRect( x, y, w, h, back );
	CG_DrawRect( x, y, w, h, 1, frame );
	/* the health bar */
	CG_FillRect( x + 4, y + 20, ( w - 8 ) * fill, 8, bar );
	CG_DrawRect( x + 4, y + 20, w - 8, 8, 1, frame );
	label = va( "%s %3i%%", t->label, health );
	CG_DrawStringExt( (int)x + 4, (int)y + 4, label, text, qtrue, qtrue, 8, 12, 0 );
	if ( seat == OAX_VEH_SEAT_DRIVER ) {
		if ( vp.active && vp.ent == num ) {
			speed = (int)( vp.speed < 0 ? -vp.speed : vp.speed );
		} else {
			speed = (int)VectorLength( cg_entities[num].currentState.pos.trDelta );
		}
		CG_DrawStringExt( (int)( x + w ) - 4 - 8 * 8, (int)y + 4, va( "%4i u/s", speed ), text, qtrue, qtrue, 8, 12, 0 );
	} else {
		CG_DrawStringExt( (int)( x + w ) - 4 - 6 * 8, (int)y + 4, "GUNNER", text, qtrue, qtrue, 8, 12, 0 );
	}
	BG_OAXDebugSet( "cg_veh_hud", va( "%i %i %i %i", num, seat, health, seat == OAX_VEH_SEAT_DRIVER ? speed : -1 ) );

	/* the gun's user: heat, or the reload, in a strip above the panel */
	if ( BG_VehGunSeat( cg_entities[num].currentState.generic1 ) == seat ) {
		const bgVehicleGun_t *g = BG_VehicleGun( t->gun );
		int heat = cg_entities[num].currentState.clientNum;
		float f;
		vec4_t gbar = { 1, 0.8f, 0.2f, 0.9f };
		if ( g->heatPerShot > 0 ) {
			f = ( heat & 127 ) / 127.0f;
			gbar[0] = 1;
			gbar[1] = 0.85f - 0.7f * f;
			gbar[2] = 0.2f;
			if ( heat & OAX_VEH_HEAT_LOCK ) {
				gbar[1] = ( cg.time / 150 ) & 1 ? 0.1f : 0.5f;
			}
		} else {
			f = 1.0f - (float)( cg.time - vehGun[num].flashTime ) / g->intervalMs;
			if ( f < 0 ) {
				f = 0;
			}
			gbar[0] = 0.5f;
			gbar[1] = 0.8f;
			gbar[2] = 1;
		}
		CG_FillRect( x, y - 12, w, 9, back );
		CG_FillRect( x + 2, y - 10, ( w - 4 ) * f, 5, gbar );
		CG_DrawRect( x, y - 12, w, 9, 1, frame );
		BG_OAXDebugSet( "cg_veh_gun", va( "%i %i %i", cg_entities[num].currentState.powerups, heat, vehGun[num].flashTime > 0 ? cg.time - vehGun[num].flashTime : -1 ) );
	}
}

/*
=================
CG_OAXVehicleCrosshair

Called by CG_DrawCrosshair: qtrue when seated (nothing more for it to
draw). The gun's user gets a reticle where the gun's line meets the world;
other seats none (nothing to aim).
=================
*/
static qboolean CG_VehToScreen( const vec3_t p, float *x, float *y ) {
	vec3_t d;
	float z, xs, ys;

	VectorSubtract( p, cg.refdef.vieworg, d );
	z = DotProduct( d, cg.refdef.viewaxis[0] );
	if ( z < 1 ) {
		return qfalse;
	}
	xs = DotProduct( d, cg.refdef.viewaxis[1] ) / z / tan( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	ys = DotProduct( d, cg.refdef.viewaxis[2] ) / z / tan( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	*x = cg.refdef.x + cg.refdef.width * 0.5f * ( 1.0f - xs );
	*y = cg.refdef.y + cg.refdef.height * 0.5f * ( 1.0f - ys );
	return qtrue;
}

qboolean CG_OAXVehicleCrosshair( void ) {
	int seat = 0, num = CG_VehSeat( &seat ), heat;
	centity_t *cent;
	vec3_t org, angles, axis[3], muzzle, dir, end;
	trace_t tr;
	float x, y, size;
	vec4_t color = { 1, 1, 1, 0.9f };
	qhandle_t sh;

	if ( num < 0 ) {
		return qfalse;
	}
	cent = &cg_entities[num];
	if ( BG_VehGunSeat( cent->currentState.generic1 ) != seat || !cg_drawCrosshair.integer ) {
		return qtrue;
	}
	CG_VehLerp( cent, org, angles );
	AnglesToAxis( angles, axis );
	BG_VehGunAim( cent->currentState.generic1, org, axis, cg.predictedPlayerState.viewangles, NULL, NULL, muzzle, dir );
	VectorMA( muzzle, 8192, dir, end );
	CG_Trace( &tr, muzzle, NULL, NULL, end, num, MASK_SHOT );
	if ( !CG_VehToScreen( tr.endpos, &x, &y ) ) {
		return qtrue;
	}
	heat = cent->currentState.clientNum;
	if ( heat & OAX_VEH_HEAT_LOCK ) {
		color[1] = color[2] = 0.25f;
	} else {
		color[1] = color[2] = 1.0f - 0.6f * ( heat & 127 ) / 127.0f;
	}
	size = 28 * cgs.screenYScale;
	sh = cgs.media.crosshairShader[( cg_drawCrosshair.integer - 1 ) % NUM_CROSSHAIRS];
	trap_R_SetColor( color );
	trap_R_DrawStretchPic( x - size * 0.5f, y - size * 0.5f, size, size, 0, 0, 1, 1, sh );
	trap_R_SetColor( NULL );
	BG_OAXDebugSet( "cg_veh_reticle", va( "%.1f %.1f", x, y ) );
	return qtrue;
}
