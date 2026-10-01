/*
===========================================================================
g_oax_stats.c: match statistics for map validation.

Which item entities (pickups and flags) a living player has touched, and
the lowest point any living bot reached, published as debug values so
a test can prove a converted map is playable: every pickup and flag
reachable, nobody falling out of the world.
===========================================================================
*/
#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"

int BotPointAreaNum( vec3_t origin );

static byte oaxReached[MAX_GENTITIES];
static float oaxMinZ;
static int oaxMinZSet;
static int oaxRoutesDone;

void G_OAXStatsInit( void ) {
	memset( oaxReached, 0, sizeof( oaxReached ) );
	oaxMinZSet = 0;
	oaxMinZ = 0;
	oaxRoutesDone = 0;
}

/* Touch_Item: a living client touched this item (grabbable or not) */
void G_OAXItemReached( gentity_t *item ) {
	oaxReached[item - g_entities] = 1;
}

static int G_OAXIsSpawn( const gentity_t *ent ) {
	return !Q_stricmp( ent->classname, "info_player_deathmatch" ) ||
		!Q_stricmp( ent->classname, "team_CTF_redspawn" ) || !Q_stricmp( ent->classname, "team_CTF_bluespawn" ) ||
		!Q_stricmp( ent->classname, "team_CTF_redplayer" ) || !Q_stricmp( ent->classname, "team_CTF_blueplayer" );
}

/*
=================
G_OAXRoutes

Once the bots' navigation (AAS) is loaded: for every item, is there a
route from at least one spawn point? Uses botlib's own travel times with
the default travel flags (walk, jump, ladders, teleporters, jump pads,
elevators), so "routable" means a bot can get there.
=================
*/
static void G_OAXRoutes( void ) {
	int spawnArea[MAX_GENTITIES / 4];
	vec3_t spawnOrigin[MAX_GENTITIES / 4];
	int numSpawns = 0, i, j, total = 0, routable = 0, len = 0, anyTravel = 0;
	char missing[1024];
	gentity_t *ent;

	for ( i = MAX_CLIENTS; i < level.num_entities && numSpawns < MAX_GENTITIES / 4; i++ ) {
		ent = &g_entities[i];
		if ( !ent->classname || !G_OAXIsSpawn( ent ) ) {
			continue;
		}
		VectorCopy( ent->s.origin, spawnOrigin[numSpawns] );
		spawnArea[numSpawns] = BotPointAreaNum( spawnOrigin[numSpawns] );
		if ( spawnArea[numSpawns] ) {
			numSpawns++;
		}
	}
	missing[0] = '\0';
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		int area, ok = 0;
		vec3_t o;

		ent = &g_entities[i];
		if ( !ent->inuse || !ent->item || ( ent->flags & FL_DROPPED_ITEM ) ) {
			continue;
		}
		total++;
		VectorCopy( ent->s.origin, o );
		area = BotPointAreaNum( o );
		for ( j = 0; j < numSpawns && area && !ok; j++ ) {
			ok = area == spawnArea[j] ||
				trap_AAS_AreaTravelTimeToGoalArea( spawnArea[j], spawnOrigin[j], area, TFL_DEFAULT ) > 0;
		}
		if ( !ok ) {
			/* diagnostic: would any travel at all (rocket jumps, hooks) get there? */
			for ( j = 0; j < numSpawns && area && !ok; j++ ) {
				if ( trap_AAS_AreaTravelTimeToGoalArea( spawnArea[j], spawnOrigin[j], area, 0x07fffffe & ~TFL_DONOTENTER ) > 0 ) {
					anyTravel++;
					break;
				}
			}
		}
		if ( ok ) {
			routable++;
		} else if ( len < (int)sizeof( missing ) - 64 ) {
			Com_sprintf( missing + len, sizeof( missing ) - len, "%s%s@%i,%i,%i%s", len ? ";" : "",
				ent->classname, (int)o[0], (int)o[1], (int)o[2], !area ? "(no area)" : !trap_AAS_AreaReachability( area ) ? "(area has no reachabilities)" : "" );
			len = strlen( missing );
		}
	}
	BG_OAXDebugSetInt( "g_route_spawns", numSpawns );
	BG_OAXDebugSetInt( "g_items_routable", routable );
	BG_OAXDebugSetInt( "g_items_routable_any_travel", routable + anyTravel );
	BG_OAXDebugSetInt( "g_items_routed_total", total );
	BG_OAXDebugSet( "g_items_unroutable", missing );
}

void G_OAXStatsFrame( void ) {
	int i, total = 0, reached = 0, flags = 0, flagsReached = 0, len = 0;
	char missing[1024];
	gentity_t *ent;

	for ( i = 0; i < level.maxclients; i++ ) {
		ent = &g_entities[i];
		/* bots only: a test may teleport the local player anywhere */
		if ( !ent->inuse || !ent->client || ent->health <= 0 || !( ent->r.svFlags & SVF_BOT ) ||
			ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		if ( !oaxMinZSet || ent->r.currentOrigin[2] < oaxMinZ ) {
			oaxMinZ = ent->r.currentOrigin[2];
			oaxMinZSet = 1;
		}
	}
	if ( !oaxRoutesDone && level.time > 3000 && trap_AAS_Initialized() ) {
		oaxRoutesDone = 1;
		G_OAXRoutes();
	}
	if ( level.framenum % 10 ) {
		return;
	}

	missing[0] = '\0';
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || !ent->item || ( ent->flags & FL_DROPPED_ITEM ) ) {
			continue;
		}
		total++;
		if ( ent->item->giType == IT_TEAM ) {
			flags++;
		}
		if ( oaxReached[i] ) {
			reached++;
			if ( ent->item->giType == IT_TEAM ) {
				flagsReached++;
			}
		} else if ( len < (int)sizeof( missing ) - 64 ) {
			Com_sprintf( missing + len, sizeof( missing ) - len, "%s%s@%i,%i,%i", len ? ";" : "",
				ent->classname, (int)ent->s.origin[0], (int)ent->s.origin[1], (int)ent->s.origin[2] );
			len = strlen( missing );
		}
	}
	BG_OAXDebugSetInt( "g_items_total", total );
	BG_OAXDebugSetInt( "g_items_reached", reached );
	BG_OAXDebugSetInt( "g_flags_total", flags );
	BG_OAXDebugSetInt( "g_flags_reached", flagsReached );
	BG_OAXDebugSet( "g_items_missing", missing );
	BG_OAXDebugSet( "g_min_z", oaxMinZSet ? va( "%i", (int)oaxMinZ ) : "none" );
	BG_OAXDebugSetInt( "g_level_time", level.time );
}
