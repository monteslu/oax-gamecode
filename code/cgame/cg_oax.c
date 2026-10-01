/*
===========================================================================
cg_oax.c: entry points for the oax map features in the cgame module.

CG_Init and CG_DrawActiveFrame call these; every feature hooks in here
instead of editing the stock flow. Features check BG_OAXFeature() for the
engine side they need and stay off on a stock engine.
===========================================================================
*/
#include "cg_local.h"

void CG_OAXInit( void ) {
	BG_OAXDebugSet( "cg_oax", va( "%i", OAX_VERSION ) );
}

/* after the scene's entities are added, before it is rendered */
void CG_OAXFrame( void ) {
}
