/*
===========================================================================
g_oax_fx.c: map entities for the oax effects (phase 6).

func_oax_emitter: a particle system (a particle decl from the engine's
.prt files in particles/) placed in a map. Keys:
  particle   the decl name
  angles     or angle: turns the decl's axes (its z is "up")
  target     instead of angles: the decl's z axis points at the target
  scale      size and distance scale (default 1)
  _color     tint r g b, 0..1 (default 1 1 1)
  seed       any integer (default: the entity number)
  spawnflags 1 START_OFF; 2 ONESHOT: every `use` restarts the system
             (its decl should have finite `cycles`)
`use` toggles a looping emitter on and off. A looping emitter runs on the
shader clock, so r_fixedShaderTime freezes it (render tests); a one-shot
runs from the time it was used.

Entity state of ET_OAX_EMITTER: s.generic1 the decl's index in
CS_OAX_FXDECLS, s.frame 1 on, s.time the start time (ONESHOT) or 0,
s.constantLight the tint (bytes), s.angles2 = scale, seed, flags.

Trails on any entity: the key `oaxtrail` "shader width lifeMs r g b a"
lists the entity in CS_OAX_TRAILS; the cgame draws a ribbon behind it
from its trajectory history.
===========================================================================
*/
#include "g_local.h"

static int trailEntities;

/* before the map's entities spawn: no emitter decls or trails yet */
void G_OAXFxReset( void ) {
	trap_SetConfigstring( CS_OAX_FXDECLS, "" );
	trap_SetConfigstring( CS_OAX_TRAILS, "" );
	trailEntities = 0;
}

#define EMITTER_START_OFF   1
#define EMITTER_ONESHOT     2

/*
=================
G_OAXFxDeclIndex

The index of a particle decl name in CS_OAX_FXDECLS, appended if new.
=================
*/
static int G_OAXFxDeclIndex( const char *name ) {
	char    list[MAX_STRING_CHARS];
	char    *p, *tok;
	int     index = 0;

	trap_GetConfigstring( CS_OAX_FXDECLS, list, sizeof( list ) );
	p = list;
	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		if ( !Q_stricmp( tok, name ) ) {
			return index;
		}
		index++;
	}
	if ( index >= 255 || strlen( list ) + strlen( name ) + 2 >= sizeof( list ) ) {
		G_Printf( "func_oax_emitter: too many particle decls\n" );
		return -1;
	}
	if ( list[0] ) {
		Q_strcat( list, sizeof( list ), " " );
	}
	Q_strcat( list, sizeof( list ), name );
	trap_SetConfigstring( CS_OAX_FXDECLS, list );
	return index;
}

static void G_OAXEmitterPublish( gentity_t *ent ) {
	BG_OAXDebugSetInt( va( "g_emitter_%d_on", ent->s.number ), ent->s.frame );
	BG_OAXDebugSetInt( va( "g_emitter_%d_time", ent->s.number ), ent->s.time );
}

static void G_OAXEmitterUse( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	if ( self->spawnflags & EMITTER_ONESHOT ) {
		self->s.frame = 1;
		self->s.time = level.time;
	} else {
		self->s.frame = !self->s.frame;
	}
	G_OAXEmitterPublish( self );
}

/* aim the decl's z axis at the target, once every entity has spawned */
static void G_OAXEmitterAim( gentity_t *ent ) {
	gentity_t   *target;
	vec3_t      dir;

	ent->think = 0;
	target = G_PickTarget( ent->target );
	if ( !target ) {
		G_Printf( "func_oax_emitter at %s: no target %s\n", vtos( ent->s.origin ), ent->target );
		return;
	}
	VectorSubtract( target->s.origin, ent->s.origin, dir );
	vectoangles( dir, ent->s.angles );
	/* vectoangles points x along dir; the decl's z is up: pitch it 90 more */
	ent->s.angles[PITCH] += 90;
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
}

void SP_func_oax_emitter( gentity_t *ent ) {
	char    *decl;
	vec3_t  color;
	float   scale;
	int     seed, index;

	G_SpawnString( "particle", "", &decl );
	if ( !decl[0] || !BG_OAXFeature( "particles" ) ) {
		if ( !decl[0] ) {
			G_Printf( "func_oax_emitter at %s: no particle key\n", vtos( ent->s.origin ) );
		}
		G_FreeEntity( ent );
		return;
	}
	index = G_OAXFxDeclIndex( decl );
	if ( index < 0 ) {
		G_FreeEntity( ent );
		return;
	}
	G_SpawnVector( "_color", "1 1 1", color );
	G_SpawnFloat( "scale", "1", &scale );
	G_SpawnInt( "seed", va( "%d", ent->s.number ), &seed );

	ent->s.eType = ET_OAX_EMITTER;
	ent->s.generic1 = index;
	ent->s.frame = ( ent->spawnflags & ( EMITTER_START_OFF | EMITTER_ONESHOT ) ) ? 0 : 1;
	ent->s.time = 0;
	ent->s.constantLight = ( (int)( color[0] * 255 ) & 255 ) | ( ( (int)( color[1] * 255 ) & 255 ) << 8 )
		| ( ( (int)( color[2] * 255 ) & 255 ) << 16 );
	ent->s.angles2[0] = scale;
	ent->s.angles2[1] = seed;
	ent->s.angles2[2] = ( ent->spawnflags & EMITTER_ONESHOT ) ? 1 : 0;
	G_SpawnVector( "angles", "0 0 0", ent->s.angles );
	if ( !ent->s.angles[0] && !ent->s.angles[1] && !ent->s.angles[2] ) {
		float angle;

		if ( G_SpawnFloat( "angle", "0", &angle ) ) {
			ent->s.angles[YAW] = angle;
		}
	}
	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	ent->use = G_OAXEmitterUse;
	if ( ent->target ) {
		ent->think = G_OAXEmitterAim;
		ent->nextthink = level.time + FRAMETIME;
	}
	trap_LinkEntity( ent );
	G_OAXEmitterPublish( ent );
}

/*
=================
G_OAXFxSpawnEntity

From G_SpawnGEntityFromSpawnVars after any map entity spawned: the
`oaxtrail` key.
=================
*/
void G_OAXFxSpawnEntity( gentity_t *ent ) {
	char    *s;
	char    list[MAX_STRING_CHARS];
	char    *rec;

	if ( !ent || !ent->inuse || !G_SpawnString( "oaxtrail", "", &s ) || !s[0] ) {
		return;
	}
	if ( !BG_OAXFeature( "trails" ) ) {
		return;
	}
	trap_GetConfigstring( CS_OAX_TRAILS, list, sizeof( list ) );
	rec = va( "%d %s;", ent->s.number, s );
	if ( strlen( list ) + strlen( rec ) + 1 >= sizeof( list ) ) {
		G_Printf( "oaxtrail: CS_OAX_TRAILS is full\n" );
		return;
	}
	Q_strcat( list, sizeof( list ), rec );
	trap_SetConfigstring( CS_OAX_TRAILS, list );
	trailEntities++;
	BG_OAXDebugSetInt( "g_trail_entities", trailEntities );
}
