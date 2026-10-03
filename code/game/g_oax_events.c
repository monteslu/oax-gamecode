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
g_oax_events.c: the script events the game module executes.

Every event here is registered with the engine's script VM at map start
(G_OAXRegisterEvents) and declared for scripts in
script/oax_events.script. Names, argument formats and behavior follow
DOOM-3's events where an id Tech 4 event exists (game/Entity.cpp,
Mover.cpp, Light.cpp, Script_Thread.cpp); the implementations are new,
written for Quake III entities.

Format characters (D3 Event.h): d int, f float, v vector, s string,
e entity (a null one ends the calling thread), E entity or null.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_script.h"

void Use_BinaryMover( gentity_t *ent, gentity_t *other, gentity_t *activator );
char *G_AddSpawnVarToken( const char *string );
void G_SpawnGEntityFromSpawnVars( void );
void ReturnToPos1( gentity_t *ent );

qboolean	( *g_oaxScriptLightHook )( gentity_t *light, const char *which, const float *values, int numValues );
void		( *g_oaxShaderParmHook )( gentity_t *ent, int parm, float value );

/* per-entity state the stock gentity_t has no room for */
typedef struct {
	const char	*owner;			/* classname pointer when set (slot reuse check) */
	int			hiddenContents;
	qboolean	hidden;
	int			bindMaster;		/* handle */
	vec3_t		bindOffset;
	qboolean	locked;
	void		( *lockedUse )( gentity_t *self, gentity_t *other, gentity_t *activator );
	float		shaderParms[12];
} oaxEntState_t;

static oaxEntState_t	entState[MAX_GENTITIES];

/* D3 sys.setSpawnArg / sys.spawn */
#define MAX_SCRIPT_SPAWNARGS	32
static char		spawnArgKeys[MAX_SCRIPT_SPAWNARGS][MAX_QPATH];
static char		spawnArgValues[MAX_SCRIPT_SPAWNARGS][MAX_STRING_CHARS / 4];
static int		numSpawnArgs;

static oaxEntState_t *EntState( gentity_t *ent ) {
	oaxEntState_t *st = &entState[ent - g_entities];

	if ( st->owner != ent->classname ) {
		memset( st, 0, sizeof( *st ) );
		st->owner = ent->classname;
	}
	return st;
}

static gentity_t *World( void ) {
	return &g_entities[ENTITYNUM_WORLD];
}

/*
===========================================================================
entity events (D3 game/Entity.cpp)
===========================================================================
*/

/* D3 idEntity::Event_Remove */
static void Ev_Remove( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	if ( self->client || self - g_entities == ENTITYNUM_WORLD ) {
		return;
	}
	G_FreeEntity( self );
}

/* D3 idEntity::Event_Hide: not drawn and not solid */
static void Ev_Hide( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	oaxEntState_t *st = EntState( self );

	if ( st->hidden ) {
		return;
	}
	st->hidden = qtrue;
	st->hiddenContents = self->r.contents;
	self->r.contents = 0;
	self->r.svFlags |= SVF_NOCLIENT;
	trap_LinkEntity( self );
}

/* D3 idEntity::Event_Show */
static void Ev_Show( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	oaxEntState_t *st = EntState( self );

	if ( !st->hidden ) {
		return;
	}
	st->hidden = qfalse;
	self->r.contents = st->hiddenContents;
	self->r.svFlags &= ~SVF_NOCLIENT;
	trap_LinkEntity( self );
}

/* D3 idEntity::Event_Activate: the entity's "use" */
static void Ev_Activate( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *activator = G_OAXArgEntity( call, 0 );

	if ( !activator ) {
		activator = World();
	}
	if ( self->use ) {
		self->use( self, activator, activator );
	}
}

static void Ev_GetOrigin( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXRetVector( ret, self->r.currentOrigin );
}

static void Ev_SetOrigin( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	vec3_t v;

	G_OAXArgVector( call, 0, v );
	G_SetOrigin( self, v );
	trap_LinkEntity( self );
}

static void Ev_GetAngles( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXRetVector( ret, self->r.currentAngles );
}

static void Ev_SetAngles( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	vec3_t v;

	G_OAXArgVector( call, 0, v );
	self->s.apos.trType = TR_STATIONARY;
	self->s.apos.trTime = level.time;
	VectorCopy( v, self->s.apos.trBase );
	VectorClear( self->s.apos.trDelta );
	VectorCopy( v, self->r.currentAngles );
	VectorCopy( v, self->s.angles );
	trap_LinkEntity( self );
}

static void Ev_GetName( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXRetString( ret, rs, self->targetname ? self->targetname : "" );
}

static void Ev_SetKey( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXSetEntityKey( self, G_OAXArgString( call, 0 ), G_OAXArgString( call, 1 ) );
}

/* D3 idEntity::Event_StartSoundShader (a sound file here) */
static void Ev_StartSound( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	const char *s = G_OAXArgString( call, 0 );

	if ( s[0] ) {
		G_AddEvent( self, EV_GENERAL_SOUND, G_SoundIndex( (char *)s ) );
	}
}

/* D3 idEntity::Event_Bind: follow master, keeping today's offset */
static void Ev_Bind( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t		*master = G_OAXArgEntity( call, 0 );
	oaxEntState_t	*st = EntState( self );

	if ( !master || master == self ) {
		return;
	}
	st->bindMaster = G_OAXScriptHandle( master );
	VectorSubtract( self->r.currentOrigin, master->r.currentOrigin, st->bindOffset );
}

static void Ev_Unbind( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	EntState( self )->bindMaster = 0;
}

/* D3 idEntity::Event_SetShaderParm */
static void Ev_SetShaderParm( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXSetShaderParm( self, G_OAXArgInt( call, 0 ), G_OAXArgFloat( call, 1 ) );
}

/*
===========================================================================
lights (D3 game/Light.cpp events; the light feature fills the hook)
===========================================================================
*/

static void LightEvent( gentity_t *self, const oaxScriptCall_t *call, const char *which ) {
	float	values[4];
	int		i, n = 0;

	for ( i = 0; i < call->argc && n < 4; i++ ) {
		if ( call->args[i].type == OAX_SV_VECTOR ) {
			values[n++] = call->args[i].f[0];
			if ( n < 4 ) values[n++] = call->args[i].f[1];
			if ( n < 4 ) values[n++] = call->args[i].f[2];
		} else {
			values[n++] = G_OAXArgFloat( call, i );
		}
	}
	if ( g_oaxScriptLightHook && g_oaxScriptLightHook( self, which, values, n ) ) {
		return;
	}
	BG_OAXDebugSet( "g_script_light", va( "%d %s %d", G_OAXScriptHandle( self ), which, n ) );
}

static void Ev_On( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) { LightEvent( self, call, "on" ); }
static void Ev_Off( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) { LightEvent( self, call, "off" ); }
static void Ev_FadeIn( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) { LightEvent( self, call, "fadeInLight" ); }
static void Ev_FadeOut( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) { LightEvent( self, call, "fadeOutLight" ); }
static void Ev_SetColor( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) { LightEvent( self, call, "setColor" ); }
static void Ev_SetLightParm( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) { LightEvent( self, call, "setLightParm" ); }

/*
===========================================================================
doors (D3 game/Mover.cpp idDoor events, on Quake III binary movers)
===========================================================================
*/

static qboolean IsBinaryMover( gentity_t *ent ) {
	return ent->s.eType == ET_MOVER && ent->moverState >= MOVER_POS1 && ent->moverState <= MOVER_2TO1 && ent->reached;
}

static void Ev_Open( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *master = self->teammaster ? self->teammaster : self;

	if ( !IsBinaryMover( master ) || EntState( master )->locked ) {
		return;
	}
	if ( master->moverState == MOVER_POS1 || master->moverState == MOVER_2TO1 ) {
		Use_BinaryMover( master, World(), World() );
	}
}

static void Ev_Close( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *master = self->teammaster ? self->teammaster : self;

	if ( !IsBinaryMover( master ) ) {
		return;
	}
	if ( master->moverState == MOVER_POS2 ) {
		ReturnToPos1( master );
	} else if ( master->moverState == MOVER_1TO2 ) {
		Use_BinaryMover( master, World(), World() );
	}
}

/* D3 idDoor::Event_Lock: a locked door ignores triggers and use */
static void Ev_Lock( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t		*master = self->teammaster ? self->teammaster : self;
	oaxEntState_t	*st = EntState( master );
	qboolean		lock = G_OAXArgInt( call, 0 ) != 0;

	if ( lock && !st->locked ) {
		st->locked = qtrue;
		st->lockedUse = master->use;
		master->use = NULL;
	} else if ( !lock && st->locked ) {
		st->locked = qfalse;
		master->use = st->lockedUse;
	}
}

static void Ev_IsOpen( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *master = self->teammaster ? self->teammaster : self;

	G_OAXRetFloat( ret, IsBinaryMover( master ) && master->moverState != MOVER_POS1 ? 1.0f : 0.0f );
}

static void Ev_IsLocked( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *master = self->teammaster ? self->teammaster : self;

	G_OAXRetFloat( ret, EntState( master )->locked ? 1.0f : 0.0f );
}

/* D3 idMover::Event_OpenPortal / Event_ClosePortal */
static void Ev_OpenPortal( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXPortalSet( self, qtrue );
}

static void Ev_ClosePortal( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXPortalSet( self, qfalse );
}

/*
===========================================================================
sys events the game executes (D3 game/script/Script_Thread.cpp)
===========================================================================
*/

/* D3 idThread::Event_GetEntity */
static void Ev_GetEntity( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	const char *name = G_OAXArgString( call, 0 );

	if ( name[0] == '$' ) {
		name++;
	}
	G_OAXRetEntity( ret, name[0] ? G_Find( NULL, FOFS( targetname ), name ) : NULL );
}

/* D3 idThread::Event_Trigger: activate an entity */
static void Ev_Trigger( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *ent = G_OAXArgEntity( call, 0 );

	if ( ent && ent->use ) {
		ent->use( ent, World(), World() );
	}
}

/* D3 idThread::Event_Say */
static void Ev_Say( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	const char *text = G_OAXArgString( call, 0 );

	trap_SendServerCommand( -1, va( "print \"%s\n\"", text ) );
	BG_OAXDebugSet( "g_script_say", text );
}

/* D3 idThread::Event_SetSpawnArg */
static void Ev_SetSpawnArg( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	if ( numSpawnArgs >= MAX_SCRIPT_SPAWNARGS ) {
		return;
	}
	Q_strncpyz( spawnArgKeys[numSpawnArgs], G_OAXArgString( call, 0 ), sizeof( spawnArgKeys[0] ) );
	Q_strncpyz( spawnArgValues[numSpawnArgs], G_OAXArgString( call, 1 ), sizeof( spawnArgValues[0] ) );
	numSpawnArgs++;
}

static gentity_t *lastSpawned;

/*
D3 idThread::Event_Spawn: spawn classname with the keys set by
setSpawnArg, as if the map had it (G_SpawnGEntityFromSpawnVars)
*/
static void Ev_Spawn( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	int i;

	level.numSpawnVars = 0;
	level.numSpawnVarChars = 0;
	level.spawnVars[0][0] = G_AddSpawnVarToken( "classname" );
	level.spawnVars[0][1] = G_AddSpawnVarToken( G_OAXArgString( call, 0 ) );
	level.numSpawnVars = 1;
	for ( i = 0; i < numSpawnArgs && level.numSpawnVars < MAX_SPAWN_VARS; i++ ) {
		level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken( spawnArgKeys[i] );
		level.spawnVars[level.numSpawnVars][1] = G_AddSpawnVarToken( spawnArgValues[i] );
		level.numSpawnVars++;
	}
	numSpawnArgs = 0;

	lastSpawned = NULL;
	G_SpawnGEntityFromSpawnVars();
	level.numSpawnVars = 0;
	if ( lastSpawned && lastSpawned->inuse ) {
		if ( lastSpawned->targetname ) {
			trap_OAX_ScriptSetEntity( lastSpawned->targetname, G_OAXScriptHandle( lastSpawned ) );
		}
		G_OAXRetEntity( ret, lastSpawned );
	} else {
		G_OAXRetEntity( ret, NULL );
	}
}

/* called by G_OAXSpawnEntity's caller chain: remembers the entity sys.spawn made */
void G_OAXEventsSpawned( gentity_t *ent ) {
	lastSpawned = ent;
}

/*
===========================================================================
shared helpers
===========================================================================
*/

void G_OAXSetShaderParm( gentity_t *ent, int parm, float value ) {
	if ( !ent || parm < 0 || parm >= 12 ) {
		return;
	}
	EntState( ent )->shaderParms[parm] = value;
	if ( g_oaxShaderParmHook ) {
		g_oaxShaderParmHook( ent, parm, value );
	}
	BG_OAXDebugSet( va( "g_shaderparm_%d", parm ), va( "%d %.4f", G_OAXScriptHandle( ent ), value ) );
}

/*
================
G_OAXSetEntityKey

D3 idTarget_SetKeyVal / idEntity spawnArgs.Set + UpdateChangeableSpawnArgs,
for the keys a Quake III entity keeps after spawning.
================
*/
qboolean G_OAXSetEntityKey( gentity_t *ent, const char *key, const char *value ) {
	vec3_t v;

	if ( !ent || !key || !key[0] ) {
		return qfalse;
	}
	if ( !Q_stricmp( key, "origin" ) ) {
		if ( sscanf( value, "%f %f %f", &v[0], &v[1], &v[2] ) == 3 ) {
			G_SetOrigin( ent, v );
			trap_LinkEntity( ent );
		}
	} else if ( !Q_stricmp( key, "angles" ) ) {
		if ( sscanf( value, "%f %f %f", &v[0], &v[1], &v[2] ) == 3 ) {
			VectorCopy( v, ent->s.angles );
			VectorCopy( v, ent->s.apos.trBase );
			VectorCopy( v, ent->r.currentAngles );
			trap_LinkEntity( ent );
		}
	} else if ( !Q_stricmp( key, "angle" ) ) {
		VectorSet( v, 0, atof( value ), 0 );
		VectorCopy( v, ent->s.angles );
		VectorCopy( v, ent->s.apos.trBase );
		VectorCopy( v, ent->r.currentAngles );
	} else if ( !Q_stricmp( key, "speed" ) ) {
		ent->speed = atof( value );
	} else if ( !Q_stricmp( key, "wait" ) ) {
		ent->wait = atof( value );
	} else if ( !Q_stricmp( key, "count" ) ) {
		ent->count = atoi( value );
	} else if ( !Q_stricmp( key, "dmg" ) ) {
		ent->damage = atoi( value );
	} else if ( !Q_stricmp( key, "health" ) ) {
		ent->health = atoi( value );
	} else if ( !Q_stricmp( key, "spawnflags" ) ) {
		ent->spawnflags = atoi( value );
	} else if ( !Q_stricmp( key, "target" ) ) {
		ent->target = value[0] ? G_NewString( value ) : NULL;
	} else if ( !Q_stricmp( key, "targetname" ) ) {
		ent->targetname = value[0] ? G_NewString( value ) : NULL;
		if ( ent->targetname ) {
			trap_OAX_ScriptSetEntity( ent->targetname, G_OAXScriptHandle( ent ) );
		}
	} else if ( !Q_stricmp( key, "message" ) ) {
		ent->message = value[0] ? G_NewString( value ) : NULL;
	} else if ( !Q_stricmpn( key, "shaderParm", 10 ) ) {
		G_OAXSetShaderParm( ent, atoi( key + 10 ), atof( value ) );
	} else {
		return qfalse;
	}
	BG_OAXDebugSet( "g_script_setkey", va( "%d %s %s", G_OAXScriptHandle( ent ), key, value ) );
	return qtrue;
}

/*
================
G_OAXBindRunFrame

D3 binding, simplified: a bound entity keeps its offset from its master's
origin (no rotation), placed after the movers moved.
================
*/
void G_OAXBindRunFrame( void ) {
	int				i;
	gentity_t		*ent, *master;
	oaxEntState_t	*st;
	vec3_t			origin;

	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		st = &entState[i];
		if ( !ent->inuse || !st->bindMaster || st->owner != ent->classname ) {
			continue;
		}
		master = G_OAXScriptEntity( st->bindMaster );
		if ( !master ) {
			st->bindMaster = 0;
			continue;
		}
		VectorAdd( master->r.currentOrigin, st->bindOffset, origin );
		G_SetOrigin( ent, origin );
		trap_LinkEntity( ent );
	}
}

/*
===========================================================================
registration
===========================================================================
*/

typedef struct {
	const char			*name;
	const char			*format;
	int					ret;
	int					flags;
	oaxScriptEventFn_t	fn;
} oaxEventEntry_t;

#define ENT		OAX_EVENT_ENTITY
#define SYS		OAX_EVENT_SYS

static const oaxEventEntry_t eventTable[] = {
	/* entities */
	{ "remove",			"",		0,		ENT, Ev_Remove },
	{ "hide",			"",		0,		ENT, Ev_Hide },
	{ "show",			"",		0,		ENT, Ev_Show },
	{ "activate",		"E",	0,		ENT, Ev_Activate },
	{ "getOrigin",		"",		'v',	ENT, Ev_GetOrigin },
	{ "setOrigin",		"v",	0,		ENT, Ev_SetOrigin },
	{ "getAngles",		"",		'v',	ENT, Ev_GetAngles },
	{ "setAngles",		"v",	0,		ENT, Ev_SetAngles },
	{ "getName",		"",		's',	ENT, Ev_GetName },
	{ "setKey",			"ss",	0,		ENT, Ev_SetKey },
	{ "startSound",		"s",	0,		ENT, Ev_StartSound },
	{ "bind",			"e",	0,		ENT, Ev_Bind },
	{ "unbind",			"",		0,		ENT, Ev_Unbind },
	{ "setShaderParm",	"df",	0,		ENT, Ev_SetShaderParm },
	/* lights */
	{ "on",				"",		0,		ENT, Ev_On },
	{ "off",			"",		0,		ENT, Ev_Off },
	{ "fadeInLight",	"f",	0,		ENT, Ev_FadeIn },
	{ "fadeOutLight",	"f",	0,		ENT, Ev_FadeOut },
	{ "setColor",		"v",	0,		ENT, Ev_SetColor },
	{ "setLightParm",	"df",	0,		ENT, Ev_SetLightParm },
	/* doors and portals */
	{ "open",			"",		0,		ENT, Ev_Open },
	{ "close",			"",		0,		ENT, Ev_Close },
	{ "lock",			"d",	0,		ENT, Ev_Lock },
	{ "isOpen",			"",		'f',	ENT, Ev_IsOpen },
	{ "isLocked",		"",		'f',	ENT, Ev_IsLocked },
	{ "openPortal",		"",		0,		ENT, Ev_OpenPortal },
	{ "closePortal",	"",		0,		ENT, Ev_ClosePortal },
	/* sys */
	{ "getEntity",		"s",	'e',	SYS, Ev_GetEntity },
	{ "trigger",		"e",	0,		SYS, Ev_Trigger },
	{ "say",			"s",	0,		SYS, Ev_Say },
	{ "setSpawnArg",	"ss",	0,		SYS, Ev_SetSpawnArg },
	{ "spawn",			"s",	'e',	SYS, Ev_Spawn },
	{ NULL, NULL, 0, 0, 0 }
};

/* other features register their own events from here (one line each) */
void G_OAXRegisterMoverEvents( void );

static void ( *const eventProviders[] )( void ) = {
	G_OAXRegisterMoverEvents,
	NULL
};

/*
================
G_OAXRegisterEvents
================
*/
void G_OAXRegisterEvents( void ) {
	const oaxEventEntry_t	*e;
	int						i;

	memset( entState, 0, sizeof( entState ) );
	numSpawnArgs = 0;
	for ( e = eventTable; e->name; e++ ) {
		G_OAXScriptRegisterEvent( e->name, e->format, e->ret, e->flags, e->fn );
	}
	for ( i = 0; eventProviders[i]; i++ ) {
		eventProviders[i]();
	}
}
