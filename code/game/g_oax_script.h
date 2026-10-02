/*
===========================================================================
g_oax_script.h: map scripting in the game module (oax "script" feature).

The id Tech 4 script VM runs in the engine (oa-engine code/idscript). The
game module feeds it level time and executes the events scripts call:
G_OAXScriptRunFrame pumps the VM once per frame (after the entities ran,
as DOOM-3 serviced script events after thinking) and answers each call
with the handler registered for that event (g_oax_events.c).

Entity handles in scripts are entity number + 1 (0 = $null_entity), as
DOOM-3 stored them.

Scripts: script/oax_main.script (includes script/oax_events.script, the
scriptEvent declarations of every event below) and maps/<mapname>.script,
both in D3 script syntax. A map without a script starts no VM, so stock
maps run exactly as before.
===========================================================================
*/
#ifndef G_OAX_SCRIPT_H
#define G_OAX_SCRIPT_H

/* syscalls (g_syscalls.asm / g_syscalls.c) */
int		trap_OAX_ScriptInit( int randomSeed );
int		trap_OAX_ScriptRegisterEvent( const char *name, const char *argfmt, int ret, int flags );
int		trap_OAX_ScriptCompileFile( const char *path );
int		trap_OAX_ScriptSetEntity( const char *name, int handle );
int		trap_OAX_ScriptStartThread( const char *func, int self );
int		trap_OAX_ScriptRun( int levelTime, oaxScriptCall_t *call );
void	trap_OAX_ScriptReturn( const oaxScriptValue_t *value, const char *string );
void	trap_OAX_ScriptObjectDone( int threadNum, int handle );
void	trap_OAX_ScriptKillThread( int threadNum );
void	trap_OAX_ScriptShutdown( void );
int		trap_OAX_ScriptNumThreads( void );

/*
 * Public API (other features call these)
 */

/* true once the map's scripts compiled and the VM runs */
qboolean	G_OAXScriptActive( void );

/*
 * Start script function func as a new thread on behalf of self (may be
 * NULL). A function with one entity parameter gets self as that argument.
 * The thread runs at the end of this server frame (or the next one when
 * called after the frame's pump). Returns the thread number, 0 when there
 * is no VM or no such function. This is what trigger "call" keys,
 * target_oax_script and in-world GUIs (runScript) use.
 */
int			G_OAXScriptCall( const char *func, gentity_t *self );

/* handle <-> entity */
int			G_OAXScriptHandle( gentity_t *ent );
gentity_t	*G_OAXScriptEntity( int handle );

/*
 * Events. A handler gets the entity the event was called on (NULL for sys
 * events), the call record (arguments) and fills *ret (and retString for
 * 's' returns, up to MAX_STRING_CHARS). Register from a provider function
 * listed in g_oaxScriptEventProviders (g_oax_events.c) so the event
 * exists before the scripts compile; declare it in script/oax_events.script.
 */
typedef void ( *oaxScriptEventFn_t )( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *retString );
int			G_OAXScriptRegisterEvent( const char *name, const char *argfmt, int ret, int flags, oaxScriptEventFn_t fn );

/* argument helpers */
float		G_OAXArgFloat( const oaxScriptCall_t *call, int i );
int			G_OAXArgInt( const oaxScriptCall_t *call, int i );
void		G_OAXArgVector( const oaxScriptCall_t *call, int i, vec3_t out );
const char	*G_OAXArgString( const oaxScriptCall_t *call, int i );
gentity_t	*G_OAXArgEntity( const oaxScriptCall_t *call, int i );
void		G_OAXRetFloat( oaxScriptValue_t *ret, float f );
void		G_OAXRetVector( oaxScriptValue_t *ret, const vec3_t v );
void		G_OAXRetEntity( oaxScriptValue_t *ret, gentity_t *ent );
void		G_OAXRetString( oaxScriptValue_t *ret, char *retString, const char *s );

/* hooks (g_oax.c, g_spawn.c, g_utils.c) */
void		G_OAXScriptInit( int levelTime, int randomSeed, int restart );
void		G_OAXScriptRunFrame( int levelTime );
void		G_OAXScriptShutdown( int restart );
void		G_OAXSpawnEntity( gentity_t *ent );					/* after a map entity spawned */
void		G_OAXScriptUseCall( gentity_t *ent, gentity_t *activator );	/* from G_UseTargets: the "call" key */

/* per-entity script data (keys the stock gentity_t has no room for) */
const char	*G_OAXEntityCall( gentity_t *ent );

/* g_oax_events.c */
void		G_OAXRegisterEvents( void );
void		G_OAXEventsSpawned( gentity_t *ent );
/*
 * Hooks other features fill (NULL by default):
 * lights: on/off/fadeIn/fadeOut/setColor/setLightParm on a light entity;
 * which is the event name, values its arguments. Return qtrue if handled.
 */
extern qboolean	( *g_oaxScriptLightHook )( gentity_t *light, const char *which, const float *values, int numValues );
/* shader parms (target_oax_shaderparm, setShaderParm): parm 0-11 */
extern void		( *g_oaxShaderParmHook )( gentity_t *ent, int parm, float value );
void		G_OAXSetShaderParm( gentity_t *ent, int parm, float value );
qboolean	G_OAXSetEntityKey( gentity_t *ent, const char *key, const char *value );

/* g_oax_triggers.c */
void SP_target_oax_script( gentity_t *ent );
void SP_trigger_oax_count( gentity_t *ent );
void SP_trigger_oax_timer( gentity_t *ent );
void SP_trigger_oax_entityname( gentity_t *ent );
void SP_target_oax_setkeyval( gentity_t *ent );
void SP_target_oax_shaderparm( gentity_t *ent );

/* g_oax_portal.c */
void SP_func_oax_portal( gentity_t *ent );
void G_OAXPortalRunFrame( void );
void G_OAXPortalSet( gentity_t *ent, qboolean open );

#endif
