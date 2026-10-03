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
bg_oax_zone.h: zone volumes (func_oax_zone), shared by game and cgame.

A zone is a brush entity whose parameters travel in configstring
CS_OAX_ZONES + slot as an infostring. Game and cgame parse the same string
with the same code and look zones up with the same collision-model routine
(a zero-size box trace against the zone's inline model), so player movement
in a zone predicts exactly.
===========================================================================
*/
#ifndef BG_OAX_ZONE_H
#define BG_OAX_ZONE_H

#define MAX_OAX_ZONES 32    /* CS_OAX_ZONES .. CS_OAX_ZONES + 31 */

/* infostring keys (short: configstrings are sent on every connect) */
typedef struct {
	int     active;         /* "on": 0 when disabled by its targetname */
	int     entityNum;      /* "e": the zone entity, the tie breaker */
	int     model;          /* "m": inline model number (*N) */
	vec3_t  origin;         /* "o": model origin (brush entities: 0 0 0) */
	vec3_t  absmin, absmax; /* "lo" "hi": bounds, a cheap reject before the CM test */
	int     priority;       /* "p": highest wins where zones overlap */

	int     hasGravity;     /* "g" absolute gravity or "gs" scale of the level's */
	float   gravity;
	float   gravityScale;

	vec3_t  current;        /* "c": ups, applied while airborne or swimming */
	vec3_t  currentDir;
	float   currentSpeed;
	float   currentAccel;   /* "ca" */

	float   frictionScale;  /* "f": scales ground/water friction, 1 = stock */

	int     hasFog;         /* "fc" "fd" "fs" "fe" */
	vec3_t  fogColor;
	float   fogDensity, fogStart, fogEnd;

	char    reverb[32];     /* "r": preset name from sound/reverb.txt */
	float   reverbDecay;    /* "rd": RT60 seconds, overrides the preset */
	float   reverbGain;     /* "rg": wet level, overrides the preset */

	int     damage;         /* "d": per second, game only */

	float   ladder;         /* "l": a ladder volume, climb speed in ups (0: not a ladder) */
} bgOAXZone_t;

extern bgOAXZone_t bg_oaxZones[MAX_OAX_ZONES];
extern int         bg_oaxNumZones;      /* highest used slot + 1 */

/* point test against one zone's model: game uses trap_EntityContact, cgame
   trap_CM_TransformedBoxTrace; both run CM_TransformedBoxTrace identically */
typedef int ( *bgOAXZoneContact_t )( const bgOAXZone_t *z, const vec3_t point );

void BG_OAXZoneParse( int slot, const char *info );
int  BG_OAXZoneAtPoint( const vec3_t point, bgOAXZoneContact_t contact );
int  BG_OAXZoneGravity( int zone, int levelGravity );

#endif
