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
#include "g_oax_mover.h"
#include "g_oax_script.h"

void G_OAXBindRunFrame( void );
void G_OAXPhysFrame( void );		/* g_oax_phys.c */
void G_OAXPhysShutdown( void );
void G_OAXCtfStatsInit( void );
void G_OAXCtfStatsFrame( void );
void G_OAXNavBotInit( void );
#include "g_oax_sim.h"

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
	G_OAXCtfStatsInit();
	G_OAXNavBotInit();		/* "nav": navmesh bots (g_oax_navbot.c) */
	BG_OAXDebugSet( "g_oax", va( "%i", OAX_VERSION ) );
	BG_OAXDebugSetInt( "g_manifest_len", len );
	G_OAXGuiInit();
	G_OAXMoverInit();
	G_OAXScriptInit( levelTime, randomSeed, restart );	/* "script" */
}

void G_OAXRunFrame( int levelTime ) {
	G_OAXStatsFrame();
	G_OAXCtfStatsFrame();
	G_OAXMoverRunFrame();
	G_OAXPortalRunFrame();		/* "portal": distance closing */
	G_OAXZoneFrame();
	G_OAXGuiRunFrame();
	G_OAXULightFrame();
	G_OAXPhysFrame();			/* "physics": the level world, physscene */
}

/* after every entity ran this frame (end of G_RunFrame) */
void G_OAXRunFrameEnd( int levelTime ) {
	G_OAXBindRunFrame();			/* script bind() */
	G_OAXScriptRunFrame( levelTime );	/* "script": the pump, as D3 serviced events after thinking */
}

void G_OAXShutdown( int restart ) {
	G_OAXScriptShutdown( restart );
	G_OAXGuiShutdown();
	G_OAXPhysShutdown();
}
