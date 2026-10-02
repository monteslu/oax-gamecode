/*
===========================================================================
g_oax_nav.h: navigation-mesh bots (g_oax_navbot.c) and the engine's
navmesh syscalls (G_OAX_NAV_*, oax_features token "nav").

Maps with heightmap terrain ship without AAS (bspc cannot see terrain);
there the engine builds a Recast/Detour navmesh and these bots path on it.
Where AAS exists, the stock bots run unchanged.
===========================================================================
*/
#ifndef G_OAX_NAV_H
#define G_OAX_NAV_H

#define OAX_NAV_PATH_PARTIAL	1

int trap_OAX_NavStatus( void );
int trap_OAX_NavFindPath( const vec3_t start, const vec3_t goal, float *points, int maxPoints, int *flags );
int trap_OAX_NavNearest( const vec3_t point, const vec3_t halfExtents, vec3_t out );
int trap_OAX_NavRandomPoint( int seed, vec3_t out );

/* nonzero when bots must use the navmesh: no AAS and the engine built a navmesh */
int G_OAXNavBotsActive( void );
qboolean G_OAXNavBotConnect( int clientNum, float skill );
void G_OAXNavBotFrame( int time );

#endif
