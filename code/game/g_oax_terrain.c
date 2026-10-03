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
g_oax_terrain.c: misc_oax_terrain (heightmap terrain).

The entity is the terrain's source in the .map: the map build bakes its
heightmap, layers and foliage into the BSPX lump OAX_TERRAIN, which the
engine's collision, renderer and navigation read. The game has nothing to
do with it at run time, so the entity frees itself (without the "no spawn
function" warning a stock game prints).
===========================================================================
*/
#include "g_local.h"

void SP_misc_oax_terrain( gentity_t *ent ) {
	G_FreeEntity( ent );
}
