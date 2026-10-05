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
cg_oax.c: entry points for the oax map features in the cgame module.

CG_Init and CG_DrawActiveFrame call these; every feature hooks in here
instead of editing the stock flow. Features check BG_OAXFeature() for the
engine side they need and stay off on a stock engine.
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_oax_traj.h"
#include "cg_oax_sim.h"

/* prediction errors over 0.1 units (cg_predict.c) */
int cg_oaxMispredicts;

/* spline configstrings as last parsed, so a change rebuilds the table */
static char cgOaxSplineCS[OAX_MAX_SPLINES][MAX_STRING_CHARS];

static void CG_OAXSplines( void ) {
	int i;
	const char *cs;

	for ( i = 0; i < OAX_MAX_SPLINES; i++ ) {
		cs = CG_ConfigString( CS_OAX_SPLINES + i );
		if ( strcmp( cs, cgOaxSplineCS[i] ) ) {
			Q_strncpyz( cgOaxSplineCS[i], cs, sizeof( cgOaxSplineCS[i] ) );
			BG_OAXSplineParse( i, cs );
		}
	}
}

/*
=================
CG_OAXTime

The time animated things are drawn at: cg.time, or cl_oaxFreezeTime (ms,
cheat, -1 = off) when the global freeze is on. The engine pins the
renderer's scene clock with the same cvar; this pins the cgame's own
clocks (sky portal, light styles, light trajectories, mover interpolation,
item bob and spin). See docs/test-hooks.md in
the oax engine (github.com/monteslu/oax-engine).
=================
*/
static vmCvar_t cl_oaxFreezeTime;
static int      freezeFrame = -1;

int CG_OAXTime( void ) {
	if ( freezeFrame != cg.clientFrame ) {
		freezeFrame = cg.clientFrame;
		trap_Cvar_Update( &cl_oaxFreezeTime );
	}
	return cl_oaxFreezeTime.integer >= 0 ? cl_oaxFreezeTime.integer : cg.time;
}

void CG_OAXInit( void ) {
	trap_Cvar_Register( &cl_oaxFreezeTime, "cl_oaxFreezeTime", "-1", CVAR_CHEAT | CVAR_TEMP );
	freezeFrame = -1;
	BG_OAXDebugSet( "cg_oax", va( "%i", OAX_VERSION ) );
	CG_OAXGuiInit();
	memset( cgOaxSplineCS, 0, sizeof( cgOaxSplineCS ) );
	cg_oaxMispredicts = 0;
	CG_OAXSplines();
	CG_OAXZoneInit();
	CG_OAXULightInit();
	CG_OAXRenderInit();
	CG_PhysInit();			/* "physics": the cosmetic world, gibs, ragdolls */
	CG_OAXVehicleInit();	/* own-vehicle prediction (cg_oax_vehicle.c) */
	CG_OAXAssaultInit();	/* the Assault HUD (cg_oax_assault.c) */
	CG_OAXFxInit();
}

/* CG_ConfigStringModified: oax configstrings (CS_OAX_*) */
void CG_OAXConfigString( int num ) {
	CG_OAXZoneConfigString( num );
	CG_OAXAssaultConfigString( num );
}

/* after the scene's entities are added, before it is rendered */
void CG_OAXFrame( void ) {
	CG_OAXGuiFrame();
	CG_OAXSplines();
	BG_OAXDebugSetInt( "cg_mispredicts", cg_oaxMispredicts );
	CG_OAXZoneFrame();
	CG_OAXRenderFrame();
	CG_PhysFrame();
	CG_OAXFxFrame();
	CG_OAXVehicleFrameEnd();
}
