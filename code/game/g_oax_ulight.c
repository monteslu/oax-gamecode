/*
===========================================================================
g_oax_ulight.c: unified lighting (phase 5), game side.

The renderer reads every light of a unified or hybrid map straight from the
entity lump, so static lights need no game entity at all (and stock
gamecode still sees them all). A light the game must control (it has a
targetname, or a `bind` key naming a mover) becomes an ET_OAX_LIGHT
entity:
  s.otherEntityNum2  the light's ordinal in the entity lump (the renderer's key)
  s.pos              its trajectory; a bound light copies its mover's, offset
  s.constantLight    color: r, g, b bytes of color * 63.75 (colors up to 4)
  s.frame            1 on, 0 off (`use` toggles; spawnflags 1 starts off)
  s.generic1         light style
The cgame turns these into trap_OAX_R_UpdateLight calls each frame.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_script.h"

/* the entity-lump ordinal of the spawn vars being spawned (g_spawn.c) */
int g_oaxSpawnOrdinal;

#define MAX_OAX_LIGHTS 256

typedef struct {
	gentity_t *ent;
	gentity_t *mover;
	vec3_t     offset;
	char       bind[64];
	qboolean   resolved;
} oaxLight_t;

static oaxLight_t oaxLights[MAX_OAX_LIGHTS];
static int        numOaxLights;

static qboolean G_OAXULightScriptEvent( gentity_t *light, const char *which, const float *values, int numValues );

void G_OAXULightReset( void ) {
	numOaxLights = 0;
	g_oaxSpawnOrdinal = 0;
	g_oaxScriptLightHook = G_OAXULightScriptEvent;
}

static int PackColor( const vec3_t c ) {
	int i, b[3];

	for ( i = 0; i < 3; i++ ) {
		b[i] = (int)( c[i] * 63.75f + 0.5f );
		if ( b[i] < 0 ) {
			b[i] = 0;
		} else if ( b[i] > 255 ) {
			b[i] = 255;
		}
	}
	return b[0] | ( b[1] << 8 ) | ( b[2] << 16 ) | ( 255 << 24 );
}

/*
=================
G_OAXULightScriptEvent

The map script's light events ($light.on(), off(), setColor(r g b), and
fadeInLight / fadeOutLight, which switch at once in v1) on lights the game
controls. Other entities are left to the script layer's default.
=================
*/
static qboolean G_OAXULightScriptEvent( gentity_t *light, const char *which, const float *values, int numValues ) {
	vec3_t c;

	if ( !light || light->s.eType != ET_OAX_LIGHT ) {
		return qfalse;
	}
	if ( !Q_stricmp( which, "on" ) || !Q_stricmp( which, "fadeInLight" ) ) {
		light->s.frame = 1;
	} else if ( !Q_stricmp( which, "off" ) || !Q_stricmp( which, "fadeOutLight" ) ) {
		light->s.frame = 0;
	} else if ( !Q_stricmp( which, "setColor" ) && numValues >= 3 ) {
		VectorCopy( values, c );
		light->s.constantLight = PackColor( c );
	} else {
		return qfalse;
	}
	BG_OAXDebugSetInt( va( "g_ulight_%d_on", light->s.otherEntityNum2 ), light->s.frame );
	return qtrue;
}

static void G_OAXLightUse( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	self->s.frame = !self->s.frame;
	BG_OAXDebugSetInt( va( "g_ulight_%d_on", self->s.otherEntityNum2 ), self->s.frame );
}

/*
=================
G_OAXLightSpawn

From SP_light (classnames light and rtlight). Returns qtrue if the light
became a game-controlled ET_OAX_LIGHT entity.
=================
*/
qboolean G_OAXLightSpawn( gentity_t *ent ) {
	char *bind;
	vec3_t color;
	oaxLight_t *ol;

	if ( !BG_OAXFeature( "ulight" ) ) {
		return qfalse;
	}
	G_SpawnString( "bind", "", &bind );
	if ( !ent->targetname && !bind[0] ) {
		return qfalse;  /* static: the renderer has it */
	}
	if ( numOaxLights == MAX_OAX_LIGHTS ) {
		G_Printf( "G_OAXLightSpawn: MAX_OAX_LIGHTS\n" );
		return qfalse;
	}

	if ( !G_SpawnVector( "_color", "1 1 1", color ) ) {
		G_SpawnVector( "color", "1 1 1", color );
	}

	ent->s.eType = ET_OAX_LIGHT;
	ent->s.otherEntityNum2 = g_oaxSpawnOrdinal;
	ent->s.constantLight = PackColor( color );
	ent->s.frame = ( ent->spawnflags & 1 ) ? 0 : 1;
	G_SpawnInt( "style", "0", &ent->s.generic1 );
	G_SetOrigin( ent, ent->s.origin );
	ent->r.svFlags |= SVF_BROADCAST;
	ent->use = G_OAXLightUse;
	trap_LinkEntity( ent );

	ol = &oaxLights[numOaxLights++];
	memset( ol, 0, sizeof( *ol ) );
	ol->ent = ent;
	Q_strncpyz( ol->bind, bind, sizeof( ol->bind ) );
	return qtrue;
}

/*
=================
G_OAXULightFrame

Bound lights follow their mover: the light's trajectory is the mover's,
offset by where the light sat relative to it at spawn.
=================
*/
void G_OAXULightFrame( void ) {
	int i, numBound = 0;

	for ( i = 0; i < numOaxLights; i++ ) {
		oaxLight_t *ol = &oaxLights[i];
		gentity_t *ent = ol->ent;

		if ( !ent->inuse || ent->s.eType != ET_OAX_LIGHT || !ol->bind[0] ) {
			continue;
		}
		if ( !ol->resolved ) {
			ol->resolved = qtrue;
			ol->mover = G_Find( NULL, FOFS( targetname ), ol->bind );
			if ( !ol->mover ) {
				G_Printf( "light %d: bind target '%s' not found\n", ent->s.otherEntityNum2, ol->bind );
				continue;
			}
			VectorSubtract( ent->s.origin, ol->mover->s.pos.trBase, ol->offset );
		}
		if ( !ol->mover || !ol->mover->inuse ) {
			continue;
		}
		ent->s.pos = ol->mover->s.pos;
		VectorAdd( ent->s.pos.trBase, ol->offset, ent->s.pos.trBase );
		BG_EvaluateTrajectory( &ent->s.pos, level.time, ent->r.currentOrigin );
		trap_LinkEntity( ent );
		numBound++;
	}
	BG_OAXDebugSetInt( "g_ulights", numOaxLights );
	BG_OAXDebugSetInt( "g_ulights_bound", numBound );
}
