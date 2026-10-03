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
