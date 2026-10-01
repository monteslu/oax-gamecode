/*
===========================================================================
g_oax.c: entry points for the oax map features in the game module.

G_InitGame, G_RunFrame and G_ShutdownGame call these once each; every
feature hooks in here instead of editing the stock flow. Features check
BG_OAXFeature() for the engine side they need and stay off on a stock
engine.
===========================================================================
*/
#include "g_local.h"

static char oaxManifest[1024];

/* the map's OAX_MANIFEST BSPX lump (JSON), "" when the map has none */
const char *G_OAXManifest( void ) {
	return oaxManifest;
}

void G_OAXInit( int levelTime, int randomSeed, int restart ) {
	int len = -1;

	oaxManifest[0] = '\0';
	if ( BG_OAXFeature( "bspx" ) ) {
		len = trap_OAX_BSPXRead( "OAX_MANIFEST", oaxManifest, sizeof( oaxManifest ) - 1 );
		if ( len >= 0 && len < (int)sizeof( oaxManifest ) ) {
			oaxManifest[len] = '\0';
		} else {
			oaxManifest[sizeof( oaxManifest ) - 1] = '\0';
		}
	}
	G_OAXStatsInit();
	BG_OAXDebugSet( "g_oax", va( "%i", OAX_VERSION ) );
	BG_OAXDebugSetInt( "g_manifest_len", len );
}

void G_OAXRunFrame( int levelTime ) {
	G_OAXStatsFrame();
}

void G_OAXShutdown( int restart ) {
}
