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
g_oax_sim.h: game-module entry points of the oax simulation features:
zone volumes (g_oax_zone.c) and seamless warp zones (g_oax_warp.c).
===========================================================================
*/
#ifndef G_OAX_SIM_H
#define G_OAX_SIM_H

/* g_oax_zone.c */
void SP_func_oax_zone( gentity_t *ent );
gentity_t *G_OAXZoneLocation( gentity_t *ent );
void G_OAXZonePmove( pmove_t *pm );          /* ClientThink_real, before Pmove */
void G_OAXZoneFrame( void );                 /* G_OAXRunFrame */

/* g_oax_warp.c */
void G_OAXWarpSpawn( gentity_t *self );      /* SP_trigger_teleport, spawnflag 4 */
void G_OAXWarpMissile( gentity_t *ent, vec3_t origin );   /* G_RunMissile */

#endif
