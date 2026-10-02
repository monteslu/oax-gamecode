/*
===========================================================================
g_oax_terrain.c: misc_oax_terrain (phase 7, heightmap terrain).

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
