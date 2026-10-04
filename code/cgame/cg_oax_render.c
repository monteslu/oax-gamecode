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
cg_oax_render.c: the cgame side of the oax world-rendering features.

- Sky portals (token "skyportal"): when the map has a misc_oax_skyportal
  (CS_OAX_INFO sp* keys), every frame first renders the sky room from the
  portal camera (RDF_OAX_SKYPORTAL), turned by `rotate` * cg.time, then
  flags the main scene RDF_OAX_UNDERSKY so its skyportal sky is not drawn
  over it. The renderer keeps one entity list for both scenes and splits
  it by BSP area.
- Light styles (token "lightstyle"): evaluates each switched or animated
  light (CS_OAX_LIGHTSTYLES) at cg.time and hands the value to the
  renderer.
- View fog (token "viewfog"): cg_oaxViewFog "r g b density start end"
  (cheat) sets the fog through the same syscall the zone volumes use.
- cg_oaxSkyPortalTime (cheat, ms, -1 = off) pins the sky rotation time;
  otherwise the global freeze (cl_oaxFreezeTime, CG_OAXTime) does.
===========================================================================
*/
#include "cg_local.h"

typedef struct {
	char	cs[MAX_INFO_STRING];
	int		active;
	vec3_t	origin;
	vec3_t	rotate;
	vec3_t	anchor;
	float	fov;
	float	scale;
} cgSkyPortal_t;

typedef struct {
	char	cs[256];
	int		on;
	char	pattern[128];
	int		len;
	int		rate;
	float	last;
} cgLightStyle_t;

static cgSkyPortal_t	skyPortal;
static cgLightStyle_t	lightStyles[OAX_LIGHTSTYLE_COUNT];
static vmCvar_t			cg_oaxViewFog;
static vmCvar_t			cg_oaxSkyPortalTime;
static char				lastViewFog[MAX_CVAR_VALUE_STRING];
static vmCvar_t			cg_oaxUnderwaterFog;
static int				underwaterFog;
static int				haveSkyPortal, haveLightStyle, haveViewFog;

static void CG_OAXParseVec( const char *s, vec3_t v ) {
	VectorClear( v );
	if ( s[0] ) {
		sscanf( s, "%f %f %f", &v[0], &v[1], &v[2] );
	}
}

static void CG_OAXUpdateSkyPortal( void ) {
	const char *info = CG_ConfigString( CS_OAX_INFO );
	const char *sp;

	if ( !strcmp( info, skyPortal.cs ) ) {
		return;
	}
	Q_strncpyz( skyPortal.cs, info, sizeof( skyPortal.cs ) );
	sp = Info_ValueForKey( info, "sp" );
	skyPortal.active = sp[0] != '\0';
	CG_OAXParseVec( sp, skyPortal.origin );
	CG_OAXParseVec( Info_ValueForKey( info, "spr" ), skyPortal.rotate );
	CG_OAXParseVec( Info_ValueForKey( info, "spa" ), skyPortal.anchor );
	skyPortal.fov = atof( Info_ValueForKey( info, "spf" ) );
	skyPortal.scale = atof( Info_ValueForKey( info, "sps" ) );
}

/*
=================
CG_OAXDrawSkyPortal

Renders the sky room before the main scene. The entities already added
this frame are shared with the main scene (the renderer splits them).
=================
*/
static void CG_OAXDrawSkyPortal( void ) {
	refdef_t	rd;
	vec3_t		angles, axis[3], d;
	float		t;
	int			i;

	if ( !skyPortal.active || ( cg.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_HYPERSPACE ) ) ) {
		return;
	}
	// CG_DrawActive draws no 3D view in these cases
	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR && ( cg.snap->ps.pm_flags & PMF_SCOREBOARD ) ) {
		return;
	}

	rd = cg.refdef;
	rd.time = cg.time;
	rd.rdflags = RDF_OAX_SKYPORTAL;
	memset( rd.areamask, 0, sizeof( rd.areamask ) );

	VectorCopy( skyPortal.origin, rd.vieworg );
	if ( skyPortal.scale > 0 ) {
		VectorSubtract( cg.refdef.vieworg, skyPortal.anchor, d );
		VectorMA( rd.vieworg, 1.0f / skyPortal.scale, d, rd.vieworg );
	}

	// the sky room turns by rotate * t: the camera turns the other way.
	// cg_oaxSkyPortalTime (cheat, ms) pins t for tests.
	trap_Cvar_Update( &cg_oaxSkyPortalTime );
	if ( cg_oaxSkyPortalTime.integer >= 0 ) {
		t = ( cg_oaxSkyPortalTime.integer % 3600000 ) * 0.001f;
	} else {
		t = ( CG_OAXTime() % 3600000 ) * 0.001f;
	}
	for ( i = 0; i < 3; i++ ) {
		angles[i] = AngleMod( skyPortal.rotate[i] * t );
	}
	AnglesToAxis( angles, axis );
	for ( i = 0; i < 3; i++ ) {
		rd.viewaxis[i][0] = DotProduct( axis[0], cg.refdef.viewaxis[i] );
		rd.viewaxis[i][1] = DotProduct( axis[1], cg.refdef.viewaxis[i] );
		rd.viewaxis[i][2] = DotProduct( axis[2], cg.refdef.viewaxis[i] );
	}

	if ( skyPortal.fov > 0 ) {
		float x = rd.width / tan( skyPortal.fov / 360 * M_PI );

		rd.fov_x = skyPortal.fov;
		rd.fov_y = atan2( rd.height, x ) * 360 / M_PI;
	}

	trap_R_RenderScene( &rd );
	cg.refdef.rdflags |= RDF_OAX_UNDERSKY;

	BG_OAXDebugSetInt( "cg_skyportal", 1 );
	BG_OAXDebugSetFloat( "cg_skyportal_yaw", angles[YAW] );
}

static void CG_OAXUpdateLightStyles( void ) {
	int		i;
	float	v;

	for ( i = 0; i < OAX_LIGHTSTYLE_COUNT; i++ ) {
		cgLightStyle_t *ls = &lightStyles[i];
		const char *cs = CG_ConfigString( CS_OAX_LIGHTSTYLES + i );

		if ( !cs[0] ) {
			continue;
		}
		if ( strcmp( cs, ls->cs ) ) {
			char *p = ls->cs;

			Q_strncpyz( ls->cs, cs, sizeof( ls->cs ) );
			ls->on = atoi( COM_Parse( &p ) );
			Q_strncpyz( ls->pattern, COM_Parse( &p ), sizeof( ls->pattern ) );
			ls->rate = atoi( COM_Parse( &p ) );
			if ( !strcmp( ls->pattern, "-" ) ) {
				ls->pattern[0] = '\0';
			}
			ls->len = strlen( ls->pattern );
			if ( ls->rate <= 0 ) {
				ls->rate = 10;
			}
			ls->last = -1;
		}

		if ( !ls->on ) {
			v = 0;
		} else if ( !ls->len ) {
			v = 1;
		} else {
			int c = ls->pattern[( CG_OAXTime() / ( 1000 / ls->rate ) ) % ls->len];

			if ( c < 'a' ) {
				c = 'a';
			} else if ( c > 'z' ) {
				c = 'z';
			}
			v = ( c - 'a' ) / 12.0f;
		}

		trap_OAX_R_SetLightStyle( OAX_LIGHTSTYLE_FIRST + i, v, v, v );
		if ( v != ls->last ) {
			ls->last = v;
			BG_OAXDebugSetFloat( va( "cg_lightstyle%i", OAX_LIGHTSTYLE_FIRST + i ), v );
			BG_OAXDebugSetInt( va( "cg_lightstyle%i_time", OAX_LIGHTSTYLE_FIRST + i ), cg.time );
		}
	}
}

static void CG_OAXUpdateViewFogCvar( void ) {
	float v[6];

	trap_Cvar_Update( &cg_oaxViewFog );
	if ( !strcmp( cg_oaxViewFog.string, lastViewFog ) ) {
		return;
	}
	Q_strncpyz( lastViewFog, cg_oaxViewFog.string, sizeof( lastViewFog ) );
	memset( v, 0, sizeof( v ) );
	if ( lastViewFog[0] ) {
		sscanf( lastViewFog, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5] );
	}
	trap_OAX_R_SetViewFog( v, v[3], v[4], v[5] );
}

void CG_OAXRenderInit( void ) {
	haveSkyPortal = BG_OAXFeature( "skyportal" );
	haveLightStyle = BG_OAXFeature( "lightstyle" );
	haveViewFog = BG_OAXFeature( "viewfog" );
	memset( &skyPortal, 0, sizeof( skyPortal ) );
	memset( lightStyles, 0, sizeof( lightStyles ) );
	lastViewFog[0] = '\0';
	trap_Cvar_Register( &cg_oaxViewFog, "cg_oaxViewFog", "", CVAR_CHEAT );
	trap_Cvar_Register( &cg_oaxUnderwaterFog, "cg_oaxUnderwaterFog", "", CVAR_ARCHIVE );
	underwaterFog = 0;
	trap_Cvar_Register( &cg_oaxSkyPortalTime, "cg_oaxSkyPortalTime", "-1", CVAR_CHEAT );
	if ( haveSkyPortal ) {
		CG_OAXUpdateSkyPortal();
	}
}

/* after the scene's entities are added, before the main scene renders */
/*
CG_OAXUpdateUnderwater: on a map whose worldspawn sets "oax_underwaterfog"
("r g b density", or "1" for a green-grey pond), with the eye in water the
view fog is the water's (exponential from the eye; cg_oaxUnderwaterFog, when
set, overrides the map's), so being under the surface looks like it. Leaving
the water clears it; a zone's fog or cg_oaxViewFog comes back on the next
frame. Maps without the key keep the stock look.
*/
static void CG_OAXUpdateUnderwater( void ) {
	vec3_t	col;
	float	density;

	const char *map = CG_OAXWorldspawnValue( "oax_underwaterfog" );

	if ( map && ( CG_PointContents( cg.refdef.vieworg, -1 ) & CONTENTS_WATER ) ) {
		trap_Cvar_Update( &cg_oaxUnderwaterFog );
		col[0] = 0.16f;
		col[1] = 0.27f;
		col[2] = 0.23f;
		density = 0.011f;
		if ( sscanf( cg_oaxUnderwaterFog.string[0] ? cg_oaxUnderwaterFog.string : map, "%f %f %f %f",
				&col[0], &col[1], &col[2], &density ) < 4 ) {
			/* "1" (or anything short of four numbers): the default */
			col[0] = 0.16f;
			col[1] = 0.27f;
			col[2] = 0.23f;
			density = 0.011f;
		}
		trap_OAX_R_SetViewFog( col, density, 0, 0 );
		if ( !underwaterFog ) {
			BG_OAXDebugSetInt( "cg_underwater", 1 );
		}
		underwaterFog = 1;
	} else if ( underwaterFog ) {
		VectorClear( col );
		trap_OAX_R_SetViewFog( col, 0, 0, 0 );
		lastViewFog[0] = '\001';	/* send cg_oaxViewFog again */
		BG_OAXDebugSetInt( "cg_underwater", 0 );
		underwaterFog = 0;
	}
}

void CG_OAXRenderFrame( void ) {
	if ( haveLightStyle ) {
		CG_OAXUpdateLightStyles();
	}
	if ( haveViewFog ) {
		CG_OAXUpdateViewFogCvar();
		CG_OAXUpdateUnderwater();
	}
	if ( haveSkyPortal ) {
		CG_OAXUpdateSkyPortal();
		CG_OAXDrawSkyPortal();
	}
}
