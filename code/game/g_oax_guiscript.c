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
