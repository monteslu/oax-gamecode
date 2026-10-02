/*
===========================================================================
g_oax_vehicle.h: server-side vehicles (g_oax_vehicle.c) and bot driving
(g_oax_vehbot.c). Vehicle CTF is the server rule g_oaxVehicles 1 on a CTF
game (g_gametype 4) and a map with info_oax_vehicle spawners; with the rule
off, spawners free themselves and the game is classic.
===========================================================================
*/
#ifndef G_OAX_VEHICLE_H
#define G_OAX_VEHICLE_H

#include "bg_oax_phys.h"
#include "bg_oax_vehicle.h"

/* the engine's vehicle and terrain calls (oax_phys.h, block 1260-1269) */
int		trap_Phys_VehicleCreate( int world, const oaxPhysVehicleDef_t *def );
void	trap_Phys_VehicleDestroy( int vehicle );
void	trap_Phys_VehicleSetInput( int vehicle, const oaxPhysVehicleInput_t *in );
int		trap_Phys_VehicleGetState( int vehicle, oaxPhysVehicleState_t *out );
int		trap_Phys_WorldAddTerrain( int world, const oaxPhysShapeDef_t *material, int *bodies, int max );

void	G_OAXVehicleInit( void );
void	G_OAXVehicleFrameBegin( void );
void	G_OAXVehicleFrame( void );
void	G_OAXVehicleShutdown( void );
/* ClientThink_real, before pmove: enter/exit, seats, driving input */
void	G_OAXVehicleClientThink( gentity_t *ent, usercmd_t *ucmd );
void	SP_info_oax_vehicle( gentity_t *ent );
/* G_Damage: a hit or an explosion pushes a vehicle */
void	G_OAXVehicleImpulse( gentity_t *veh, const vec3_t dir, const vec3_t point, int knockback );

/* queries for bots */
int		G_OAXVehiclesActive( void );
int		G_OAXVehicleOfClient( int clientNum, int *seat );		/* vehicle entity number, -1 on foot */
int		G_OAXVehicleCount( void );
gentity_t *G_OAXVehicleEnt( int i );								/* i-th live vehicle, NULL past the end */
int		G_OAXVehicleSeatFree( gentity_t *veh, int seat );
const oaxPhysVehicleState_t *G_OAXVehicleState( gentity_t *veh );
int		G_OAXVehicleType( gentity_t *veh );
float	G_OAXVehicleReach( gentity_t *veh );						/* how close a player must be to get in */

/* g_oax_vehbot.c: called by the navmesh bots on their command each frame */
void	G_OAXVehBotInit( void );
void	G_OAXVehBotCommand( int clientNum, usercmd_t *cmd, const vec3_t goal, int haveGoal, int time );
void	G_OAXVehBotFrame( void );

#endif
