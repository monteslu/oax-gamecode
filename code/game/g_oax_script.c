/*
===========================================================================
g_oax_script.c: map scripting in the game module (see g_oax_script.h).

New code for oax (GPLv3, as the id Tech 4 code it works with). The parts
that follow DOOM-3 behavior (a trigger's "call" key starts a thread on
activation, the map script's main() starts with the map, $name binds to
the entity named name) are noted where they happen.

Pump: once per server frame, after the entities ran (G_OAXScriptRunFrame
from the end of G_RunFrame), trap_OAX_ScriptRun runs every due script
thread at level.time. Each event a thread calls on the game comes back
here as a call record; its handler runs and the result goes back with
trap_OAX_ScriptReturn, until RUN reports nothing more to do this frame.

Random numbers in scripts (sys.random) come from the VM's own generator,
seeded from the map name and g_scriptSeed (not from G_InitGame's seed,
which is the wall clock on a native build), so a map's scripts make the
same choices on every build.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_script.h"

#define MAX_SCRIPT_EVENTS	512
#define MAX_CALL_LEN		64

static qboolean				scriptActive;
static oaxScriptCall_t		scriptCall;
static char					scriptRetString[MAX_STRING_CHARS];
static oaxScriptEventFn_t	eventFns[MAX_SCRIPT_EVENTS];
static int					eventFlags[MAX_SCRIPT_EVENTS];

/* per-entity "call" keys; owner is the classname pointer of the entity that
   set it, so a reused slot reads as empty */
static char					entCall[MAX_GENTITIES][MAX_CALL_LEN];
static const char			*entCallOwner[MAX_GENTITIES];

static vmCvar_t				g_scriptSeed;
static vmCvar_t				g_oaxScripts;		/* 0: never start the VM (cost comparisons) */

/*
================
G_OAXScriptActive
================
*/
qboolean G_OAXScriptActive( void ) {
	return scriptActive;
}

/*
================
handles: entity number + 1, 0 = none (D3: entityNumber + 1)
================
*/
int G_OAXScriptHandle( gentity_t *ent ) {
	if ( !ent ) {
		return 0;
	}
	return ( ent - g_entities ) + 1;
}

gentity_t *G_OAXScriptEntity( int handle ) {
	gentity_t *ent;

	if ( handle <= 0 || handle > MAX_GENTITIES ) {
		return NULL;
	}
	ent = &g_entities[handle - 1];
	if ( !ent->inuse ) {
		return NULL;
	}
	return ent;
}

/*
================
G_OAXScriptRegisterEvent
================
*/
int G_OAXScriptRegisterEvent( const char *name, const char *argfmt, int ret, int flags, oaxScriptEventFn_t fn ) {
	int num;

	num = trap_OAX_ScriptRegisterEvent( name, argfmt, ret, flags );
	if ( num < 0 || num >= MAX_SCRIPT_EVENTS ) {
		G_Printf( S_COLOR_YELLOW "script: couldn't register event %s\n", name );
		return -1;
	}
	eventFns[num] = fn;
	eventFlags[num] = flags;
	return num;
}

/*
================
argument helpers
================
*/
float G_OAXArgFloat( const oaxScriptCall_t *call, int i ) {
	if ( i < 0 || i >= call->argc ) {
		return 0.0f;
	}
	if ( call->args[i].type == OAX_SV_INT || call->args[i].type == OAX_SV_ENTITY ) {
		return (float)call->args[i].i;
	}
	return call->args[i].f[0];
}

int G_OAXArgInt( const oaxScriptCall_t *call, int i ) {
	if ( i < 0 || i >= call->argc ) {
		return 0;
	}
	if ( call->args[i].type == OAX_SV_FLOAT ) {
		return (int)call->args[i].f[0];
	}
	return call->args[i].i;
}

void G_OAXArgVector( const oaxScriptCall_t *call, int i, vec3_t out ) {
	if ( i < 0 || i >= call->argc || call->args[i].type != OAX_SV_VECTOR ) {
		VectorClear( out );
		return;
	}
	VectorCopy( call->args[i].f, out );
}

const char *G_OAXArgString( const oaxScriptCall_t *call, int i ) {
	int ofs;

	if ( i < 0 || i >= call->argc || call->args[i].type != OAX_SV_STRING ) {
		return "";
	}
	ofs = call->args[i].i;
	if ( ofs < 0 || ofs >= OAX_SCRIPT_STRINGS ) {
		return "";
	}
	return call->strings + ofs;
}

gentity_t *G_OAXArgEntity( const oaxScriptCall_t *call, int i ) {
	if ( i < 0 || i >= call->argc || call->args[i].type != OAX_SV_ENTITY ) {
		return NULL;
	}
	return G_OAXScriptEntity( call->args[i].i );
}

void G_OAXRetFloat( oaxScriptValue_t *ret, float f ) {
	ret->type = OAX_SV_FLOAT;
	ret->f[0] = f;
}

void G_OAXRetVector( oaxScriptValue_t *ret, const vec3_t v ) {
	ret->type = OAX_SV_VECTOR;
	VectorCopy( v, ret->f );
}

void G_OAXRetEntity( oaxScriptValue_t *ret, gentity_t *ent ) {
	ret->type = OAX_SV_ENTITY;
	ret->i = G_OAXScriptHandle( ent );
}

void G_OAXRetString( oaxScriptValue_t *ret, char *retString, const char *s ) {
	ret->type = OAX_SV_STRING;
	Q_strncpyz( retString, s ? s : "", MAX_STRING_CHARS );
}

/*
================
G_OAXEntityCall
================
*/
const char *G_OAXEntityCall( gentity_t *ent ) {
	int n;

	if ( !ent ) {
		return "";
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_GENTITIES || !entCall[n][0] || entCallOwner[n] != ent->classname ) {
		return "";
	}
	return entCall[n];
}

/*
================
G_OAXSpawnEntity

From G_SpawnGEntityFromSpawnVars, after a map entity spawned (level.spawnVars
still holds its keys). Records the keys oax features read later.
================
*/
void G_OAXSpawnEntity( gentity_t *ent ) {
	char	*s;
	int		n;

	if ( !ent || !ent->inuse ) {
		return;
	}
	G_OAXEventsSpawned( ent );
	n = ent - g_entities;
	entCall[n][0] = '\0';
	entCallOwner[n] = NULL;
	/* D3 idTrigger::Spawn: "call" names a script function run on activation */
	if ( G_SpawnString( "call", "", &s ) && s[0] ) {
		Q_strncpyz( entCall[n], s, sizeof( entCall[n] ) );
		entCallOwner[n] = ent->classname;
	}
}

/*
================
G_OAXScriptCall
================
*/
int G_OAXScriptCall( const char *func, gentity_t *self ) {
	int thread;

	if ( !scriptActive || !func || !func[0] ) {
		return 0;
	}
	thread = trap_OAX_ScriptStartThread( func, G_OAXScriptHandle( self ) );
	if ( !thread ) {
		G_Printf( S_COLOR_YELLOW "script: %s calls unknown function '%s'\n", self && self->classname ? self->classname : "?", func );
	}
	return thread;
}

/*
================
G_OAXScriptUseCall

From G_UseTargets: D3 idTrigger::CallScript, for any entity that fires its
targets (triggers, buttons, relays) and has a "call" key.
================
*/
void G_OAXScriptUseCall( gentity_t *ent, gentity_t *activator ) {
	const char *call;

	if ( !scriptActive ) {
		return;
	}
	call = G_OAXEntityCall( ent );
	if ( call[0] ) {
		G_OAXScriptCall( call, ent );
	}
}

/*
================
G_OAXScriptBindEntities

D3 idProgram::SetEntity for every named entity: $targetname in scripts.
================
*/
static void G_OAXScriptBindEntities( void ) {
	int			i;
	int			bound = 0;
	gentity_t	*ent;

	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		if ( !ent->inuse || !ent->targetname || !ent->targetname[0] ) {
			continue;
		}
		bound += trap_OAX_ScriptSetEntity( ent->targetname, G_OAXScriptHandle( ent ) );
	}
	BG_OAXDebugSetInt( "g_script_bound", bound );
}

/*
================
G_OAXScriptInit
================
*/
void G_OAXScriptInit( int levelTime, int randomSeed, int restart ) {
	char			map[MAX_QPATH];
	char			path[MAX_QPATH];
	fileHandle_t	f;
	int				len, seed, i;

	scriptActive = qfalse;
	memset( eventFns, 0, sizeof( eventFns ) );
	trap_Cvar_Register( &g_scriptSeed, "g_scriptSeed", "0", 0 );
	trap_Cvar_Register( &g_oaxScripts, "g_oaxScripts", "1", 0 );
	if ( !BG_OAXFeature( "script" ) || !g_oaxScripts.integer ) {
		BG_OAXDebugSetInt( "g_script_active", 0 );
		return;
	}

	trap_Cvar_VariableStringBuffer( "mapname", map, sizeof( map ) );
	Com_sprintf( path, sizeof( path ), "maps/%s.script", map );
	len = trap_FS_FOpenFile( path, &f, FS_READ );
	if ( f ) {
		trap_FS_FCloseFile( f );
	}
	if ( len <= 0 ) {
		/* no map script: no VM, the map plays as it would on a stock game */
		BG_OAXDebugSetInt( "g_script_active", 0 );
		return;
	}

	/* the seed depends only on the map and the cvar (see the file comment) */
	seed = g_scriptSeed.integer;
	for ( i = 0; map[i]; i++ ) {
		seed = seed * 31 + ( map[i] | 32 );
	}
	trap_OAX_ScriptInit( seed ? seed : 1 );
	G_OAXRegisterEvents();

	if ( trap_OAX_ScriptCompileFile( "script/oax_main.script" ) != 1 || trap_OAX_ScriptCompileFile( path ) != 1 ) {
		G_Printf( S_COLOR_RED "script: %s did not compile; map scripting is off\n", path );
		trap_OAX_ScriptShutdown();
		BG_OAXDebugSetInt( "g_script_active", 0 );
		return;
	}

	scriptActive = qtrue;
	G_OAXScriptBindEntities();

	/* D3 starts the map script's main() with the map */
	trap_OAX_ScriptStartThread( "main", 0 );
	BG_OAXDebugSetInt( "g_script_active", 1 );
}

/*
================
G_OAXScriptRunFrame

The pump (see the file comment).
================
*/
void G_OAXScriptRunFrame( int levelTime ) {
	oaxScriptValue_t	ret;
	gentity_t			*self;
	oaxScriptEventFn_t	fn;
	int					calls = 0;

	if ( !scriptActive ) {
		return;
	}
	while ( trap_OAX_ScriptRun( levelTime, &scriptCall ) ) {
		memset( &ret, 0, sizeof( ret ) );
		scriptRetString[0] = '\0';
		fn = 0;
		if ( scriptCall.event >= 0 && scriptCall.event < MAX_SCRIPT_EVENTS ) {
			fn = eventFns[scriptCall.event];
		}
		self = scriptCall.self ? G_OAXScriptEntity( scriptCall.self ) : NULL;
		/* an entity event on a removed entity returns a safe 0, as D3's did
		   for entities that don't respond */
		if ( fn && ( self || !scriptCall.self ) ) {
			fn( self, &scriptCall, &ret, scriptRetString );
		}
		trap_OAX_ScriptReturn( &ret, scriptRetString );
		calls++;
	}
	BG_OAXDebugSetInt( "g_script_frame_calls", calls );
}

/*
================
G_OAXScriptShutdown
================
*/
void G_OAXScriptShutdown( int restart ) {
	if ( scriptActive ) {
		trap_OAX_ScriptShutdown();
	}
	scriptActive = qfalse;
}
