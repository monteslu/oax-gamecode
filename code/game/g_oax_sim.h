/*
===========================================================================
g_oax_sim.h: game-module entry points of the oax simulation features:
zone volumes (g_oax_zone.c) and seamless warp zones (g_oax_warp.c).
===========================================================================
*/
#ifndef G_OAX_SIM_H
#define G_OAX_SIM_H

/* g_oax_zone.c */
void SP_func_oax_zone( gentity_t *ent );
void G_OAXZonePmove( pmove_t *pm );          /* ClientThink_real, before Pmove */
void G_OAXZoneFrame( void );                 /* G_OAXRunFrame */

/* g_oax_warp.c */
void G_OAXWarpSpawn( gentity_t *self );      /* SP_trigger_teleport, spawnflag 4 */
void G_OAXWarpMissile( gentity_t *ent, vec3_t origin );   /* G_RunMissile */

#endif
