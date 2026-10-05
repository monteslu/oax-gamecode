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
g_oax_vehbot.c: bots that drive vehicles.

The navmesh bots (g_oax_navbot.c) choose goals and fight; this file runs
on each bot's command right after the bot brain wrote it
(G_OAXVehBotCommand) and takes over when vehicles help:

- On foot, with a goal far away (enemy flag, home with the flag, ...): a
  free vehicle near the bot and not off in the wrong direction is walked
  to and boarded with the use button; a teammate's buggy passing close
  with its gunner seat free is boarded too (the gunner fights with the
  normal bot brain on the mounted gun, its movement ignored). A driver on
  the vehicle's own gun (hover craft, hover tank) fires it the same way.
- Driving: the bot follows the navmesh path to its goal (re-pathed every
  second from the vehicle) with a pure-pursuit steering controller on the
  next path corner: steer by the heading error, full throttle when lined
  up, slower in tight turns and near the goal, reverse out when stuck.
  It gets out near the goal (flags and bases are taken on foot), when the
  vehicle is wrecked or on its side, or after being stuck too often.

Everything is a function of the game state, so a match plays the same on
every build.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_vehicle.h"
#include "g_oax_nav.h"

#define VB_MAX_POINTS	32
#define VB_REPATH_MS	1000
#define VB_BOARD_GOAL	1600.0f		/* goals closer than this are walked to */
#define VB_BOARD_RANGE	900.0f		/* how far a bot walks to a vehicle */
#define VB_EXIT_GOAL	420.0f		/* get out this close to the goal */
#define VB_BOARD_MS		7000		/* give up walking to a vehicle after this */
#define VB_AVOID_MS		15000

typedef struct {
	int			target;			/* vehicle entity walked to, -1 */
	int			targetSince;
	int			avoid[MAX_GENTITIES / 32];	/* bit per entity: vehicles to leave alone */
	int			avoidUntil;
	int			usePhase;
	/* driving */
	float		pts[VB_MAX_POINTS * 3];
	int			numPts, cur, pathTime;
	vec3_t		pathGoal;
	int			slowSince;
	int			reverseUntil;
	int			stuckCount;
	float		reverseSteer;
	int			wasDriving;
	int			exitReason;		/* why the bot is getting out: 1 goal, 2 wreck, 3 stuck */
} vehBot_t;

static vehBot_t	vehBots[MAX_CLIENTS];
static int		vbBoards, vbExitsGoal, vbExitsWreck, vbExitsStuck, vbStuck, vbGunners, vbPaths, vbFlagDrives;

void G_OAXVehBotInit( void ) {
	int i;

	memset( vehBots, 0, sizeof( vehBots ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		vehBots[i].target = -1;
	}
	vbBoards = vbExitsGoal = vbExitsWreck = vbExitsStuck = vbStuck = vbGunners = vbPaths = vbFlagDrives = 0;
}

static float VB_HorizDist( const vec3_t a, const vec3_t b ) {
	float dx = a[0] - b[0], dy = a[1] - b[1];
	return sqrt( dx * dx + dy * dy );
}

static int VB_Avoided( vehBot_t *vb, int ent, int time ) {
	if ( time > vb->avoidUntil ) {
		memset( vb->avoid, 0, sizeof( vb->avoid ) );
		return 0;
	}
	return ( vb->avoid[ent >> 5] >> ( ent & 31 ) ) & 1;
}

static void VB_Avoid( vehBot_t *vb, int ent, int time ) {
	vb->avoid[ent >> 5] |= 1 << ( ent & 31 );
	vb->avoidUntil = time + VB_AVOID_MS;
}

/* press use on every other bot frame, so the vehicle code sees an edge */
static void VB_PressUse( vehBot_t *vb, usercmd_t *cmd ) {
	vb->usePhase ^= 1;
	if ( vb->usePhase ) {
		cmd->buttons |= BUTTON_USE_HOLDABLE;
	} else {
		cmd->buttons &= ~BUTTON_USE_HOLDABLE;
	}
}

/* look and walk straight at a point */
static void VB_WalkTo( gclient_t *cl, usercmd_t *cmd, const vec3_t to ) {
	vec3_t d, ang;

	VectorSubtract( to, cl->ps.origin, d );
	d[2] = 0;
	vectoangles( d, ang );
	cmd->angles[YAW] = ANGLE2SHORT( ang[YAW] ) - cl->ps.delta_angles[YAW];
	cmd->angles[PITCH] = ANGLE2SHORT( 0 ) - cl->ps.delta_angles[PITCH];
	cmd->forwardmove = 127;
	cmd->rightmove = 0;
	cmd->upmove = 0;
}

/* ---- on foot: get in ------------------------------------------------------------ */

static void VB_OnFoot( int clientNum, vehBot_t *vb, usercmd_t *cmd, const vec3_t goal, int haveGoal, int time ) {
	gentity_t *ent = &g_entities[clientNum];
	gclient_t *cl = ent->client;
	gentity_t *best = NULL;
	float bestDist = VB_BOARD_RANGE, goalDist;
	int i;

	if ( vb->wasDriving ) {
		/* just got out */
		if ( vb->exitReason == 3 ) {
			vbExitsStuck++;
		} else if ( vb->exitReason == 2 ) {
			vbExitsWreck++;
		} else {
			vbExitsGoal++;
		}
		vb->wasDriving = 0;
	}
	if ( !haveGoal ) {
		vb->target = -1;
		return;
	}
	goalDist = VB_HorizDist( cl->ps.origin, goal );
	/* walking to a vehicle already */
	if ( vb->target >= 0 ) {
		gentity_t *v = &g_entities[vb->target];
		if ( !v->inuse || v->s.eType != ET_OAX_VEHICLE || !G_OAXVehicleSeatFree( v, OAX_VEH_SEAT_DRIVER ) ||
			time - vb->targetSince > VB_BOARD_MS ) {
			if ( v->inuse && time - vb->targetSince > VB_BOARD_MS ) {
				VB_Avoid( vb, vb->target, time );
			}
			vb->target = -1;
		} else {
			VB_WalkTo( cl, cmd, v->r.currentOrigin );
			if ( Distance( cl->ps.origin, v->r.currentOrigin ) < G_OAXVehicleReach( v ) - 16.0f ) {
				cmd->forwardmove = 0;
				VB_PressUse( vb, cmd );
			}
			return;
		}
	}
	/* a teammate's buggy close by with the gunner seat free: hop on */
	for ( i = 0; i < G_OAXVehicleCount(); i++ ) {
		gentity_t *v = G_OAXVehicleEnt( i );
		int driver = v->s.otherEntityNum;
		if ( driver >= 0 && driver < MAX_CLIENTS && driver != clientNum && G_OAXVehicleSeatFree( v, OAX_VEH_SEAT_GUNNER ) &&
			OnSameTeam( ent, &g_entities[driver] ) && goalDist > VB_BOARD_GOAL &&
			Distance( cl->ps.origin, v->r.currentOrigin ) < G_OAXVehicleReach( v ) - 8.0f ) {
			VB_PressUse( vb, cmd );
			return;
		}
	}
	if ( goalDist < VB_BOARD_GOAL ) {
		return;
	}
	/* a free vehicle near us that does not take us the wrong way */
	for ( i = 0; i < G_OAXVehicleCount(); i++ ) {
		gentity_t *v = G_OAXVehicleEnt( i );
		const oaxPhysVehicleState_t *st = G_OAXVehicleState( v );
		vec3_t axis[3];
		float d;
		if ( !st || !G_OAXVehicleSeatFree( v, OAX_VEH_SEAT_DRIVER ) || !G_OAXVehicleSeatFree( v, OAX_VEH_SEAT_GUNNER ) ||
			VB_Avoided( vb, v->s.number, time ) ) {
			continue;
		}
		BG_QuatToAxis( st->quat, axis );
		if ( axis[2][2] < 0.6f || v->health * 3 < BG_VehicleType( G_OAXVehicleType( v ) )->health ) {
			continue;
		}
		d = Distance( cl->ps.origin, v->r.currentOrigin );
		if ( d < bestDist && VB_HorizDist( v->r.currentOrigin, goal ) < goalDist + 300.0f ) {
			best = v;
			bestDist = d;
		}
	}
	if ( best ) {
		vb->target = best->s.number;
		vb->targetSince = time;
		VB_WalkTo( cl, cmd, best->r.currentOrigin );
	}
}

/* ---- driving ----------------------------------------------------------------------- */

static void VB_Drive( int clientNum, vehBot_t *vb, usercmd_t *cmd, gentity_t *veh, const vec3_t goal, int haveGoal, int time ) {
	const oaxPhysVehicleState_t *st = G_OAXVehicleState( veh );
	const bgVehicleType_t *t = BG_VehicleType( G_OAXVehicleType( veh ) );
	vec3_t axis[3], to;
	float goalDist, want, have, err, steer, throttle, speed;

	if ( !st ) {
		return;
	}
	/* a driver on its vehicle's gun fights with the bot brain's aim and
	   trigger (the vehicle steers on its own); others only drive */
	cmd->buttons &= BG_VehGunSeat( G_OAXVehicleType( veh ) ) == OAX_VEH_SEAT_DRIVER ? ~BUTTON_USE_HOLDABLE : ~( BUTTON_ATTACK | BUTTON_USE_HOLDABLE );
	cmd->upmove = 0;
	if ( !vb->wasDriving ) {
		vb->wasDriving = 1;
		vb->exitReason = 0;
		vb->numPts = 0;
		vb->slowSince = 0;
		vb->reverseUntil = 0;
		vb->stuckCount = 0;
		vb->target = -1;
		vbBoards++;
		if ( g_entities[clientNum].client->ps.powerups[PW_REDFLAG] || g_entities[clientNum].client->ps.powerups[PW_BLUEFLAG] ) {
			vbFlagDrives++;
		}
	}
	BG_QuatToAxis( st->quat, axis );
	speed = st->speed;

	/* get out: at the goal, wrecked, on its side, or stuck for good */
	goalDist = haveGoal ? VB_HorizDist( st->origin, goal ) : 0;
	if ( !haveGoal || goalDist < VB_EXIT_GOAL || veh->health * 4 < t->health || axis[2][2] < 0.3f || vb->stuckCount >= 4 ) {
		if ( !haveGoal || goalDist < VB_EXIT_GOAL ) {
			vb->exitReason = 1;
		} else if ( vb->stuckCount >= 4 ) {
			vb->exitReason = 3;
			VB_Avoid( vb, veh->s.number, time );
		} else {
			vb->exitReason = 2;
		}
		cmd->forwardmove = 0;
		cmd->rightmove = 0;
		cmd->upmove = -127;		/* brake while getting out */
		cmd->buttons |= BUTTON_USE_HOLDABLE;	/* held: getting out takes a hold */
		return;
	}

	/* the path from the vehicle */
	if ( vb->numPts <= 0 || time - vb->pathTime >= VB_REPATH_MS || Distance( goal, vb->pathGoal ) > 128.0f ) {
		vec3_t from;
		int flags = 0;
		VectorCopy( st->origin, from );
		from[2] -= t->halfExtents[2] + 16.0f;
		vb->numPts = BG_OAXFeature( "nav" ) ? trap_OAX_NavFindPath( from, goal, vb->pts, VB_MAX_POINTS, &flags ) : 0;
		if ( vb->numPts <= 0 ) {
			VectorCopy( st->origin, vb->pts );
			VectorCopy( goal, &vb->pts[3] );
			vb->numPts = 2;
		}
		vb->cur = 1;
		vb->pathTime = time;
		VectorCopy( goal, vb->pathGoal );
		vbPaths++;
	}
	/* pure pursuit: the first corner farther than the lookahead */
	while ( vb->cur < vb->numPts - 1 && VB_HorizDist( st->origin, &vb->pts[vb->cur * 3] ) < 180.0f ) {
		vb->cur++;
	}
	VectorSubtract( &vb->pts[vb->cur * 3], st->origin, to );
	want = atan2( to[1], to[0] ) * ( 180.0f / M_PI );
	have = atan2( axis[0][1], axis[0][0] ) * ( 180.0f / M_PI );
	err = AngleSubtract( want, have );

	/* stuck: slow with the throttle open, then back out the other way */
	if ( time < vb->reverseUntil ) {
		cmd->forwardmove = -127;
		cmd->rightmove = (signed char)( vb->reverseSteer * 127.0f );
		return;
	}
	if ( fabs( speed ) < 60.0f ) {
		if ( !vb->slowSince ) {
			vb->slowSince = time;
		} else if ( time - vb->slowSince > 1200 ) {
			vb->slowSince = 0;
			vb->stuckCount++;
			vbStuck++;
			vb->reverseUntil = time + 1000;
			/* back out turning the wheels against the way we want to go */
			vb->reverseSteer = err > 0 ? 1.0f : -1.0f;
			vb->numPts = 0;
			return;
		}
	} else {
		vb->slowSince = 0;
		if ( speed > 300.0f && vb->stuckCount > 0 && ( time & 4095 ) < 50 ) {
			vb->stuckCount--;
		}
	}

	steer = err / 30.0f;
	if ( steer > 1.0f ) {
		steer = 1.0f;
	} else if ( steer < -1.0f ) {
		steer = -1.0f;
	}
	throttle = fabs( err ) < 35.0f ? 1.0f : fabs( err ) < 90.0f ? 0.6f : 0.35f;
	/* slow into the goal and into sharp corners */
	if ( goalDist < 1100.0f && speed > 550.0f ) {
		throttle = 0.0f;
	}
	if ( fabs( err ) > 60.0f && speed > 650.0f ) {
		throttle = -1.0f;		/* against the motion: the brakes */
	}
	cmd->forwardmove = (signed char)( throttle * 127.0f );
	cmd->rightmove = (signed char)( -steer * 127.0f );
}

/* ---- the hook ---------------------------------------------------------------------------- */

void G_OAXVehBotCommand( int clientNum, usercmd_t *cmd, const vec3_t goal, int haveGoal, int time ) {
	gentity_t *ent;
	vehBot_t *vb;
	int seat = 0, veh;

	if ( !G_OAXVehiclesActive() || clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	ent = &g_entities[clientNum];
	vb = &vehBots[clientNum];
	if ( !ent->client || ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		vb->target = -1;
		vb->wasDriving = 0;
		return;
	}
	veh = G_OAXVehicleOfClient( clientNum, &seat );
	if ( veh < 0 ) {
		VB_OnFoot( clientNum, vb, cmd, goal, haveGoal, time );
		return;
	}
	if ( seat == OAX_VEH_SEAT_DRIVER ) {
		VB_Drive( clientNum, vb, cmd, &g_entities[veh], goal, haveGoal, time );
		return;
	}
	/* gunner: fight from the deck; get off when the driver is gone or the goal is near */
	vb->wasDriving = 0;
	vbGunners++;
	cmd->buttons &= ~BUTTON_USE_HOLDABLE;
	cmd->upmove = 0;		/* jump would change seats */
	if ( g_entities[veh].s.otherEntityNum == ENTITYNUM_NONE || ( haveGoal && VB_HorizDist( ent->client->ps.origin, goal ) < VB_EXIT_GOAL ) ) {
		cmd->buttons |= BUTTON_USE_HOLDABLE;	/* held: getting out takes a hold */
	}
}

void G_OAXVehBotFrame( void ) {
	if ( !G_OAXVehiclesActive() || level.framenum % 10 ) {
		return;
	}
	BG_OAXDebugSetInt( "g_vehbot_drives", vbBoards );
	BG_OAXDebugSetInt( "g_vehbot_exit_goal", vbExitsGoal );
	BG_OAXDebugSetInt( "g_vehbot_exit_wreck", vbExitsWreck );
	BG_OAXDebugSetInt( "g_vehbot_exit_stuck", vbExitsStuck );
	BG_OAXDebugSetInt( "g_vehbot_stuck", vbStuck );
	BG_OAXDebugSetInt( "g_vehbot_gunner_frames", vbGunners );
	BG_OAXDebugSetInt( "g_vehbot_paths", vbPaths );
	BG_OAXDebugSetInt( "g_vehbot_flag_drives", vbFlagDrives );
}
