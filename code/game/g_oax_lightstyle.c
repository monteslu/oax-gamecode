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
g_oax_lightstyle.c: switchable and animated lights (light styles).

q3map2 gives every `light` with a targetname its own style (32 + n) and
keeps the entity in the BSP; the lightmap for that light is compiled as
a separate style stage that the renderer scales by the style's value
(`rgbGen lightstyle N`, written through the worldspawn keys
_styleNrgbgen). This module keeps those lights alive as style
controllers instead of freeing them:

"lightstyle"         Quake-style pattern, 10 letters a second, a = 0,
                     m = 1 (as compiled), z = 25/12
"lightstyle_preset"  1-11, the classic patterns (flicker, pulse, candle,
                     strobe, ...); ignored when "lightstyle" is set
"lightstyle_rate"    letters per second (default 10)
"start_off"          1 = starts off

Using the light (a button, trigger or script targeting it) toggles it.
The state lives in CS_OAX_LIGHTSTYLES + (style - 32) as
"<on> <pattern> <rate>"; cgame evaluates it at cg.time every frame.
===========================================================================
*/
#include "g_local.h"

char *G_NewString( const char *string );

/*
 * The classic light style patterns, from id Software's Quake (GPL), as
 * Quake 1's world.c / Quake 2's g_spawn.c set up lightstyles 1-11.
 */
static const char *lightstylePresets[] = {
	"m",
	"mmnmmommommnonmmonqnmmo",						/* 1 flicker */
	"abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",	/* 2 slow strong pulse */
	"mmmmmaaaaammmmmaaaaaabcdefgabcdefg",			/* 3 candle */
	"mamamamamama",									/* 4 fast strobe */
	"jklmnopqrstuvwxyzyxwvutsrqponmlkj",			/* 5 gentle pulse */
	"nmonqnmomnmomomno",							/* 6 flicker 2 */
	"mmmaaaabcdefgmmmmaaaammmaamm",					/* 7 candle 2 */
	"mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",	/* 8 candle 3 */
	"aaaaaaaazzzzzzzz",								/* 9 slow strobe */
	"mmamammmmammamamaaamammma",					/* 10 fluorescent flicker */
	"abcdefghijklmnopqrrqponmlkjihgfedcba"			/* 11 slow pulse, not to black */
};

#define NUM_PRESETS ( (int)( sizeof( lightstylePresets ) / sizeof( lightstylePresets[0] ) ) )

static void LightStyle_Publish( gentity_t *ent ) {
	int slot = ent->count - OAX_LIGHTSTYLE_FIRST;

	trap_SetConfigstring( CS_OAX_LIGHTSTYLES + slot,
		va( "%i %s %i", ent->health ? 1 : 0, ent->message ? ent->message : "-", ent->damage ) );
	BG_OAXDebugSetInt( va( "g_lightstyle%i", ent->count ), ent->health ? 1 : 0 );
}

static void LightStyle_Use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	ent->health = !ent->health;
	LightStyle_Publish( ent );
}

/*
=================
G_OAXLightStyleSpawn

From SP_light: qtrue when the light became a style controller (keep it),
qfalse when it should be freed as stock OpenArena does.
=================
*/
qboolean G_OAXLightStyleSpawn( gentity_t *ent ) {
	int		style, preset, startOff, rate;
	char	*pattern;

	G_SpawnInt( "style", "0", &style );
	if ( style < OAX_LIGHTSTYLE_FIRST || style >= OAX_LIGHTSTYLE_FIRST + OAX_LIGHTSTYLE_COUNT || !ent->targetname ) {
		return qfalse;
	}

	G_SpawnInt( "lightstyle_preset", "0", &preset );
	G_SpawnInt( "lightstyle_rate", "10", &rate );
	G_SpawnInt( "start_off", "0", &startOff );

	ent->message = NULL;
	if ( G_SpawnString( "lightstyle", "", &pattern ) && pattern[0] ) {
		ent->message = G_NewString( pattern );
	} else if ( preset > 0 && preset < NUM_PRESETS ) {
		ent->message = (char *)lightstylePresets[preset];
	}

	ent->count = style;
	ent->health = !startOff;
	ent->damage = rate > 0 ? rate : 10;
	ent->use = LightStyle_Use;
	ent->r.svFlags |= SVF_NOCLIENT;
	ent->takedamage = qfalse;
	LightStyle_Publish( ent );
	return qtrue;
}
