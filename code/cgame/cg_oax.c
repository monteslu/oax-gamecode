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

void CG_OAXInit( void ) {
	BG_OAXDebugSet( "cg_oax", va( "%i", OAX_VERSION ) );
	CG_OAXGuiInit();
	memset( cgOaxSplineCS, 0, sizeof( cgOaxSplineCS ) );
	cg_oaxMispredicts = 0;
	CG_OAXSplines();
	CG_OAXZoneInit();
	CG_OAXULightInit();
	CG_OAXRenderInit();
}

/* CG_ConfigStringModified: oax configstrings (CS_OAX_*) */
void CG_OAXConfigString( int num ) {
	CG_OAXZoneConfigString( num );
}

/* after the scene's entities are added, before it is rendered */
void CG_OAXFrame( void ) {
	CG_OAXGuiFrame();
	CG_OAXSplines();
	BG_OAXDebugSetInt( "cg_mispredicts", cg_oaxMispredicts );
	CG_OAXZoneFrame();
	CG_OAXRenderFrame();
}
