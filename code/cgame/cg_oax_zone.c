/*
===========================================================================
cg_oax_zone.c: zone volumes, cgame side.

The zones arrive as configstrings (CS_OAX_ZONES + slot) and are parsed by
the same BG_OAXZoneParse the game uses. Prediction looks them up with
trap_CM_TransformedBoxTrace, the routine SV_EntityContact runs for the
game, so predicted movement in a zone matches the server exactly.

The eye's zone also sets the view fog (renderer, "viewfog") and the sound
reverb (engine mixer, "reverb") while the eye is inside it.
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_oax_zone.h"
#include "cg_oax_sim.h"

static clipHandle_t zoneModels[MAX_OAX_ZONES];
static int          eyeZone = -2;
static int          fogOn;
static char         reverbKey[96];

void CG_OAXZoneConfigString( int num ) {
	int slot = num - CS_OAX_ZONES;

	if ( slot < 0 || slot >= MAX_OAX_ZONES ) {
		return;
	}
	BG_OAXZoneParse( slot, CG_ConfigString( num ) );
	zoneModels[slot] = bg_oaxZones[slot].model ? trap_CM_InlineModel( bg_oaxZones[slot].model ) : 0;
	eyeZone = -2;   /* re-apply fog and reverb */
}

void CG_OAXZoneInit( void ) {
	int i;

	for ( i = 0; i < MAX_OAX_ZONES; i++ ) {
		CG_OAXZoneConfigString( CS_OAX_ZONES + i );
	}
	BG_OAXDebugSetInt( "cg_zones", bg_oaxNumZones );
}

static int CG_OAXZoneContact( const bgOAXZone_t *z, const vec3_t point ) {
	trace_t tr;

	trap_CM_TransformedBoxTrace( &tr, vec3_origin, vec3_origin, point, point,
		zoneModels[z - bg_oaxZones], -1, z->origin, vec3_origin );
	return tr.startsolid;
}

static int CG_OAXZoneAt( const vec3_t point ) {
	return BG_OAXZoneAtPoint( point, CG_OAXZoneContact );
}

void CG_OAXZonePmove( pmove_t *pm ) {
	pm->oaxZoneAt = 0;
	if ( bg_oaxNumZones ) {
		pm->oaxZoneAt = CG_OAXZoneAt;
	}
}

/*
=================
CG_OAXZoneFrame

Fog follows the eye every frame; reverb only changes when the eye's zone
(or its parameters) changes.
=================
*/
void CG_OAXZoneFrame( void ) {
	int                zone;
	const bgOAXZone_t *z;
	char               key[96];

	if ( !bg_oaxNumZones ) {
		return;
	}
	BG_OAXDebugSetInt( "cg_zone", CG_OAXZoneAt( cg.predictedPlayerState.origin ) );
	zone = CG_OAXZoneAt( cg.refdef.vieworg );
	z = zone >= 0 ? &bg_oaxZones[zone] : NULL;
	if ( zone != eyeZone ) {
		eyeZone = zone;
		BG_OAXDebugSetInt( "cg_eyezone", zone );
		/* the fog the eye is in, whether or not the renderer can draw it */
		BG_OAXDebugSet( "cg_eyefog", z && z->hasFog ? va( "%f %f %f %f %i %i", z->fogColor[0], z->fogColor[1],
			z->fogColor[2], z->fogDensity, (int)z->fogStart, (int)z->fogEnd ) : "none" );
	}

	if ( BG_OAXFeature( "viewfog" ) ) {
		if ( z && z->hasFog ) {
			trap_OAX_R_SetViewFog( z->fogColor, z->fogDensity, z->fogStart, z->fogEnd );
			if ( !fogOn ) {
				BG_OAXDebugSetInt( "cg_fog", 1 );
			}
			fogOn = 1;
		} else if ( fogOn ) {
			vec3_t black = { 0, 0, 0 };
			trap_OAX_R_SetViewFog( black, 0, 0, 0 );
			BG_OAXDebugSetInt( "cg_fog", 0 );
			fogOn = 0;
		}
	}

	if ( BG_OAXFeature( "reverb" ) ) {
		key[0] = '\0';
		if ( z && ( z->reverb[0] || z->reverbDecay > 0 ) ) {
			Com_sprintf( key, sizeof( key ), "%s %f %f", z->reverb, z->reverbDecay, z->reverbGain );
		}
		if ( strcmp( key, reverbKey ) ) {
			Q_strncpyz( reverbKey, key, sizeof( reverbKey ) );
			if ( key[0] ) {
				trap_OAX_S_SetReverb( z->reverb, z->reverbDecay, z->reverbGain );
			} else {
				trap_OAX_S_SetReverb( "", 0, 0 );
			}
			BG_OAXDebugSet( "cg_reverb", key[0] ? key : "off" );
		}
	}
}
