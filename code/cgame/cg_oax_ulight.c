/*
===========================================================================
cg_oax_ulight.c: unified lighting (phase 5), cgame side.

Every ET_OAX_LIGHT entity the game sends updates its light in the renderer
each frame (trap_OAX_R_UpdateLight, keyed by the light's entity-lump
ordinal): position from its trajectory, color, on/off. A stock cgame never
calls this; the renderer then keeps every light where the map put it.

cg_oaxLightTime (cheat, -1 off) evaluates light trajectories at a fixed
time, so tests can render a moving light at the same place on every build.
===========================================================================
*/
#include "cg_local.h"

static vmCvar_t cg_oaxLightTime;
static int      ulightFeature = -1;

void CG_OAXULightInit( void ) {
	ulightFeature = BG_OAXFeature( "ulight" );
	trap_Cvar_Register( &cg_oaxLightTime, "cg_oaxLightTime", "-1", CVAR_CHEAT );
}

void CG_OAXLight( centity_t *cent ) {
	entityState_t *s = &cent->currentState;
	vec3_t origin, rgb;
	int t;

	if ( ulightFeature != 1 ) {
		return;
	}
	trap_Cvar_Update( &cg_oaxLightTime );
	t = cg_oaxLightTime.integer >= 0 ? cg_oaxLightTime.integer : cg.time;
	BG_EvaluateTrajectory( &s->pos, t, origin );
	rgb[0] = ( s->constantLight & 255 ) / 63.75f;
	rgb[1] = ( ( s->constantLight >> 8 ) & 255 ) / 63.75f;
	rgb[2] = ( ( s->constantLight >> 16 ) & 255 ) / 63.75f;
	trap_OAX_R_UpdateLight( s->otherEntityNum2, origin, NULL, rgb, NULL, s->frame ? 1 : 0 );
}
