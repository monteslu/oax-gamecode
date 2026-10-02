/*
===========================================================================
cg_oax_sim.h: cgame side of the oax simulation features (zone volumes).
===========================================================================
*/
#ifndef CG_OAX_SIM_H
#define CG_OAX_SIM_H

void CG_OAXZoneInit( void );
void CG_OAXZoneConfigString( int num );
void CG_OAXZonePmove( pmove_t *pm );         /* CG_PredictPlayerState */
void CG_OAXZoneFrame( void );                /* CG_OAXFrame: view fog, reverb */

void trap_OAX_R_SetViewFog( const vec3_t rgb, float density, float start, float end );
void trap_OAX_S_SetReverb( const char *preset, float decay, float wet );

#endif
