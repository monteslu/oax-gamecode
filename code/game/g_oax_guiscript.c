/*
===========================================================================
g_oax_guiscript.c: where a GUI's "runScript <function>" goes.

It starts a map-script thread running <function> (the DOOM-3 script port,
oax "script" feature), with self as the GUI's entity when the function
takes one. Without a script VM (stock engine, or a map with no
maps/<map>.script) the call is logged and nothing runs.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_script.h"

void G_OAXGuiRunScript( const char *func, gentity_t *self ) {
	if ( !G_OAXScriptCall( func, self ) ) {
		G_Printf( "func_oax_gui %i: runScript %s: no map script function to run\n", self ? self->s.number : -1, func );
	}
}
