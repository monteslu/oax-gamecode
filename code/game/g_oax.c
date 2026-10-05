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
void G_OAXPhysFrame( void );		/* g_oax_phys.c */
void G_OAXPhysShutdown( void );
void G_OAXNavBotInit( void );
#include "g_oax_sim.h"
#include "g_oax_vehicle.h"
#include "g_oax_nav.h"

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
	G_OAXTeleportInit();		/* noretrigger teleporters (g_oax_teleport.c) */
	G_OAXTranslocatorInit();	/* rule g_oaxTranslocator (g_oax_translocator.c) */
	G_OAXNavLinksInit();		/* navigation from intent (g_oax_navlinks.c) */
	BG_OAXDebugSet( "g_oax", va( "%i", OAX_VERSION ) );
	BG_OAXDebugSetInt( "g_manifest_len", len );
	G_OAXGuiInit();
	G_OAXMoverInit();
	G_OAXScriptInit( levelTime, randomSeed, restart );	/* "script" */
	G_OAXVehicleInit();			/* "physics_vehicle", rule g_oaxVehicles */
	G_OAXAssaultInit( restart );	/* g_gametype GT_ASSAULT */
	G_OAXPlaceInit();			/* exact setviewpos read-back */
}

void G_OAXRunFrame( int levelTime ) {
	G_OAXNavLinksFrame();		/* once settled: links and hazard costs into the navmesh */
	G_OAXStatsFrame();
	G_OAXCtfStatsFrame();
	G_OAXMoverRunFrame();
	G_OAXPortalRunFrame();		/* "portal": distance closing */
	G_OAXZoneFrame();
	G_OAXGuiRunFrame();
	G_OAXULightFrame();
	G_OAXVehicleFrameBegin();	/* vehicle controls, before the world steps */
	G_OAXPhysFrame();			/* "physics": the level world, physscene */
	G_OAXVehicleFrame();		/* vehicle entities and occupants from the step */
	G_OAXAssaultFrame();		/* Assault objectives and rounds */
}

/* after every entity ran this frame (end of G_RunFrame) */
void G_OAXRunFrameEnd( int levelTime ) {
	G_OAXBindRunFrame();			/* script bind() */
	G_OAXScriptRunFrame( levelTime );	/* "script": the pump, as D3 serviced events after thinking */
	G_OAXPlaceFrame();				/* setviewpos read-back */
}

void G_OAXShutdown( int restart ) {
	G_OAXScriptShutdown( restart );
	G_OAXGuiShutdown();
	G_OAXVehicleShutdown();
	G_OAXAssaultShutdown();
	G_OAXPhysShutdown();
}
