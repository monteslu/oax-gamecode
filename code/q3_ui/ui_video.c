/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
#include "ui_local.h"

void GraphicsOptions_MenuInit( void );

/*
=======================================================================

DRIVER INFORMATION MENU

=======================================================================
*/


#define DRIVERINFO_FRAMEL	"menu/" MENU_ART_DIR "/frame2_l"
#define DRIVERINFO_FRAMER	"menu/" MENU_ART_DIR "/frame1_r"
#define DRIVERINFO_BACK0	"menu/" MENU_ART_DIR "/back_0"
#define DRIVERINFO_BACK1	"menu/" MENU_ART_DIR "/back_1"

static char* driverinfo_artlist[] = 
{
	DRIVERINFO_FRAMEL,
	DRIVERINFO_FRAMER,
	DRIVERINFO_BACK0,
	DRIVERINFO_BACK1,
	NULL,
};

#define ID_DRIVERINFOBACK	100

typedef struct
{
	menuframework_s	menu;
	menutext_s		banner;
	menubitmap_s	back;
	menubitmap_s	framel;
	menubitmap_s	framer;
	char			stringbuff[1024];
	char*			strings[64];
	int				numstrings;
} driverinfo_t;

static driverinfo_t	s_driverinfo;

/*
=================
DriverInfo_Event
=================
*/
static void DriverInfo_Event( void* ptr, int event )
{
	if (event != QM_ACTIVATED)
		return;

	switch (((menucommon_s*)ptr)->id)
	{
		case ID_DRIVERINFOBACK:
			UI_PopMenu();
			break;
	}
}

/*
=================
DriverInfo_MenuDraw
=================
*/
static void DriverInfo_MenuDraw( void )
{
	int	i;
	int	y;

	Menu_Draw( &s_driverinfo.menu );

	UI_DrawString( 320, 80, "VENDOR", UI_CENTER|UI_SMALLFONT, color_red );
	UI_DrawString( 320, 152, "PIXELFORMAT", UI_CENTER|UI_SMALLFONT, color_red );
	UI_DrawString( 320, 192, "EXTENSIONS", UI_CENTER|UI_SMALLFONT, color_red );

	UI_DrawString( 320, 80+16, uis.glconfig.vendor_string, UI_CENTER|UI_SMALLFONT, text_color_normal );
	UI_DrawString( 320, 96+16, uis.glconfig.version_string, UI_CENTER|UI_SMALLFONT, text_color_normal );
	UI_DrawString( 320, 112+16, uis.glconfig.renderer_string, UI_CENTER|UI_SMALLFONT, text_color_normal );
	UI_DrawString( 320, 152+16, va ("color(%d-bits) Z(%d-bits) stencil(%d-bits)", uis.glconfig.colorBits, uis.glconfig.depthBits, uis.glconfig.stencilBits), UI_CENTER|UI_SMALLFONT, text_color_normal );

	// double column
	y = 192+16;
	for (i=0; i<s_driverinfo.numstrings/2; i++) {
		UI_DrawString( 320-4, y, s_driverinfo.strings[i*2], UI_RIGHT|UI_SMALLFONT, text_color_normal );
		UI_DrawString( 320+4, y, s_driverinfo.strings[i*2+1], UI_LEFT|UI_SMALLFONT, text_color_normal );
		y += SMALLCHAR_HEIGHT;
	}

	if (s_driverinfo.numstrings & 1)
		UI_DrawString( 320, y, s_driverinfo.strings[s_driverinfo.numstrings-1], UI_CENTER|UI_SMALLFONT, text_color_normal );
}

/*
=================
DriverInfo_Cache
=================
*/
void DriverInfo_Cache( void )
{
	int	i;

	// touch all our pics
	for (i=0; ;i++)
	{
		if (!driverinfo_artlist[i])
			break;
		trap_R_RegisterShaderNoMip(driverinfo_artlist[i]);
	}
}

/*
=================
UI_DriverInfo_Menu
=================
*/
static void UI_DriverInfo_Menu( void )
{
	char*	eptr;
	int		i;
	int		len;

	// zero set all our globals
	memset( &s_driverinfo, 0 ,sizeof(driverinfo_t) );

	DriverInfo_Cache();

	s_driverinfo.menu.fullscreen = qtrue;
	s_driverinfo.menu.draw       = DriverInfo_MenuDraw;

	s_driverinfo.banner.generic.type  = MTYPE_BTEXT;
	s_driverinfo.banner.generic.x	  = 320;
	s_driverinfo.banner.generic.y	  = 16;
	s_driverinfo.banner.string		  = "DRIVER INFO";
	s_driverinfo.banner.color	      = color_white;
	s_driverinfo.banner.style	      = UI_CENTER;

	s_driverinfo.framel.generic.type  = MTYPE_BITMAP;
	s_driverinfo.framel.generic.name  = DRIVERINFO_FRAMEL;
	s_driverinfo.framel.generic.flags = QMF_INACTIVE;
	s_driverinfo.framel.generic.x	  = 0;
	s_driverinfo.framel.generic.y	  = 78;
	s_driverinfo.framel.width  	      = 256;
	s_driverinfo.framel.height  	  = 329;

	s_driverinfo.framer.generic.type  = MTYPE_BITMAP;
	s_driverinfo.framer.generic.name  = DRIVERINFO_FRAMER;
	s_driverinfo.framer.generic.flags = QMF_INACTIVE;
	s_driverinfo.framer.generic.x	  = 376;
	s_driverinfo.framer.generic.y	  = 76;
	s_driverinfo.framer.width  	      = 256;
	s_driverinfo.framer.height  	  = 334;

	s_driverinfo.back.generic.type	   = MTYPE_BITMAP;
	s_driverinfo.back.generic.name     = DRIVERINFO_BACK0;
	s_driverinfo.back.generic.flags    = QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_driverinfo.back.generic.callback = DriverInfo_Event;
	s_driverinfo.back.generic.id	   = ID_DRIVERINFOBACK;
	s_driverinfo.back.generic.x		   = 0;
	s_driverinfo.back.generic.y		   = 480-64;
	s_driverinfo.back.width  		   = 128;
	s_driverinfo.back.height  		   = 64;
	s_driverinfo.back.focuspic         = DRIVERINFO_BACK1;

  // TTimo: overflow with particularly long GL extensions (such as the gf3)
  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=399
  // NOTE: could have pushed the size of stringbuff, but the list is already out of the screen
  // (no matter what your resolution)
  Q_strncpyz(s_driverinfo.stringbuff, uis.glconfig.extensions_string, 1024);

	// build null terminated extension strings
	eptr = s_driverinfo.stringbuff;
	while ( s_driverinfo.numstrings<40 && *eptr )
	{
		while ( *eptr == ' ' )
			*eptr++ = '\0';

		// track start of valid string
		if (*eptr && *eptr != ' ')
			s_driverinfo.strings[s_driverinfo.numstrings++] = eptr;

		while ( *eptr && *eptr != ' ' )
			eptr++;
	}

	// safety length strings for display
	for (i=0; i<s_driverinfo.numstrings; i++) {
		len = strlen(s_driverinfo.strings[i]);
		if (len > 32) {
			s_driverinfo.strings[i][len-1] = '>';
			s_driverinfo.strings[i][len]   = '\0';
		}
	}

	Menu_AddItem( &s_driverinfo.menu, &s_driverinfo.banner );
	Menu_AddItem( &s_driverinfo.menu, &s_driverinfo.framel );
	Menu_AddItem( &s_driverinfo.menu, &s_driverinfo.framer );
	Menu_AddItem( &s_driverinfo.menu, &s_driverinfo.back );

	UI_PushMenu( &s_driverinfo.menu );
}

/*
=======================================================================

GRAPHICS OPTIONS MENU

=======================================================================
*/

#define GRAPHICSOPTIONS_FRAMEL	"menu/" MENU_ART_DIR "/frame2_l"
#define GRAPHICSOPTIONS_FRAMER	"menu/" MENU_ART_DIR "/frame1_r"
#define GRAPHICSOPTIONS_BACK0	"menu/" MENU_ART_DIR "/back_0"
#define GRAPHICSOPTIONS_BACK1	"menu/" MENU_ART_DIR "/back_1"
#define GRAPHICSOPTIONS_ACCEPT0	"menu/" MENU_ART_DIR "/accept_0"
#define GRAPHICSOPTIONS_ACCEPT1	"menu/" MENU_ART_DIR "/accept_1"

#define ID_BACK2		101
#define ID_FULLSCREEN	102
#define ID_LIST			103
#define ID_MODE			104
#define ID_DRIVERINFO	105
#define ID_GRAPHICS		106
#define ID_DISPLAY		107
#define ID_SOUND		108
#define ID_NETWORK		109
#define ID_RATIO                110

typedef struct {
	menuframework_s	menu;

	menutext_s		banner;
	menubitmap_s	framel;
	menubitmap_s	framer;

	menutext_s		graphics;
	menutext_s		display;
	menutext_s		sound;
	menutext_s		network;

	menulist_s		list;
        menulist_s              ratio;
	menulist_s		mode;
	menulist_s		driver;
	menuslider_s	tq;
	menulist_s  	fs;
	menulist_s  	lighting;
        menulist_s  	flares;
        menulist_s  	bloom;
	menulist_s  	allow_extensions;
	menulist_s  	texturebits;
	menulist_s  	geometry;
	menulist_s  	filter;
        menulist_s  	aniso;
	menulist_s  	drawfps;
	menutext_s		driverinfo;

	menubitmap_s	apply;
	menubitmap_s	back;
} graphicsoptions_t;

typedef struct
{
	int mode;
	qboolean fullscreen;
	int tq;
	int lighting;
	qboolean flares;
	qboolean bloom;
	qboolean drawfps;
	int texturebits;
	int geometry;
	int filter;
        int aniso;
	int driver;
	qboolean extensions;
} InitialVideoOptions_s;

static InitialVideoOptions_s	s_ivo;
static graphicsoptions_t		s_graphicsoptions;	

static InitialVideoOptions_s s_ivo_templates[] =
{
	{
		6, qtrue, 3, 0, qfalse,qfalse,qfalse, 2, 2, 1, 0, 0, qtrue
	},
	{
		4, qtrue, 2, 0, qfalse,qfalse,qfalse, 2, 1, 1, 0, 0, qtrue	// JDC: this was tq 3
	},
	{
		3, qtrue, 2, 0, qfalse,qfalse,qfalse, 0, 1, 0, 0, 0, qtrue
	},
	{
		2, qtrue, 1, 0, qfalse,qfalse,qfalse, 0, 0, 0, 0, 0, qtrue
	},
	{
		2, qtrue, 1, 1, qfalse,qfalse,qfalse, 0, 0, 0, 0, 0, qtrue
	},
	{
		3, qtrue, 1, 0, qfalse,qfalse,qfalse, 0, 1, 0, 0, 0, qtrue
	}
};

#define NUM_IVO_TEMPLATES ( sizeof( s_ivo_templates ) / sizeof( s_ivo_templates[0] ) )

/*
Resolutions (oax): the display's modes (r_availableModes, from SDL) of at
least 800x600, plus the window's current size, ordered by aspect ratio
(widest first) and then by size. A choice applies as r_mode -1 with
r_customwidth / r_customheight, so every listed size works, whatever the
renderer's old mode table holds. Without a mode list (some platforms), a
set of common modern sizes stands in.
*/
#define MAX_RESOLUTIONS	96

typedef struct {
	int		w, h;
	float	aspect;
	char	name[16];
} uiResolution_t;

static uiResolution_t	resList[ MAX_RESOLUTIONS ];
static int				numResolutions;
static const char		*resolutions[ MAX_RESOLUTIONS + 1 ];

static char				ratioNames[ MAX_RESOLUTIONS ][ 8 ];
static const char		*ratios[ MAX_RESOLUTIONS + 1 ];
static int				numRatios;
static int				resToRatio[ MAX_RESOLUTIONS ];
static int				ratioToRes[ MAX_RESOLUTIONS ];	// the largest size of that ratio

static const struct { float aspect; const char *name; } knownRatios[ ] = {
	{ 1.25f, "5:4" }, { 4.0f / 3.0f, "4:3" }, { 1.5f, "3:2" }, { 1.6f, "16:10" },
	{ 5.0f / 3.0f, "5:3" }, { 16.0f / 9.0f, "16:9" }, { 64.0f / 27.0f, "21:9" }, { 3.2f, "32:10" },
	{ 32.0f / 9.0f, "32:9" }
};

static const int fallbackResolutions[ ][ 2 ] = {
	{ 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
	{ 1280, 800 }, { 1440, 900 }, { 1680, 1050 }, { 1920, 1200 }, { 2560, 1600 },
	{ 1024, 768 }, { 1280, 960 }, { 1280, 1024 }, { 1600, 1200 }
};

static void GraphicsOptions_RatioName( float aspect, char *out, int size )
{
	int i;

	for ( i = 0; i < ARRAY_LEN( knownRatios ); i++ ) {
		if ( fabs( aspect - knownRatios[ i ].aspect ) < 0.02f ) {
			Q_strncpyz( out, knownRatios[ i ].name, size );
			return;
		}
	}
	Com_sprintf( out, size, "%.2f:1", aspect );
}

static int GraphicsOptions_FindResolution( int w, int h )
{
	int i;

	for ( i = 0; i < numResolutions; i++ ) {
		if ( resList[ i ].w == w && resList[ i ].h == h )
			return i;
	}
	return -1;
}

static void GraphicsOptions_AddResolution( int w, int h, qboolean always )
{
	uiResolution_t *r;

	if ( w <= 0 || h <= 0 || numResolutions >= MAX_RESOLUTIONS )
		return;
	if ( !always && ( w < 800 || h < 600 ) )
		return;
	if ( GraphicsOptions_FindResolution( w, h ) >= 0 )
		return;
	r = &resList[ numResolutions++ ];
	r->w = w;
	r->h = h;
	r->aspect = (float)w / (float)h;
}

static int GraphicsOptions_CompareResolutions( const void *a, const void *b )
{
	const uiResolution_t *ra = a, *rb = b;
	char na[ 8 ], nb[ 8 ];

	// same named ratio (1366x768 is 16:9 too): by size
	GraphicsOptions_RatioName( ra->aspect, na, sizeof( na ) );
	GraphicsOptions_RatioName( rb->aspect, nb, sizeof( nb ) );
	if ( strcmp( na, nb ) && fabs( ra->aspect - rb->aspect ) > 0.001f )
		return ra->aspect > rb->aspect ? -1 : 1;
	return ra->w * ra->h - rb->w * rb->h;
}

/*
=================
GraphicsOptions_GetResolutions
=================
*/
static void GraphicsOptions_GetResolutions( void )
{
	char buf[ MAX_STRING_CHARS ], *s;
	int i, j;

	numResolutions = 0;
	trap_Cvar_VariableStringBuffer( "r_availableModes", buf, sizeof( buf ) );
	for ( s = buf; *s; ) {
		int w = atoi( s ), h = 0;
		char *x = strchr( s, 'x' );

		if ( x )
			h = atoi( x + 1 );
		GraphicsOptions_AddResolution( w, h, qfalse );
		s = strchr( s, ' ' );
		if ( !s )
			break;
		s++;
	}
	if ( !numResolutions ) {
		for ( i = 0; i < ARRAY_LEN( fallbackResolutions ); i++ )
			GraphicsOptions_AddResolution( fallbackResolutions[ i ][ 0 ], fallbackResolutions[ i ][ 1 ], qfalse );
	}
	// the size the game runs at now is always on the list
	GraphicsOptions_AddResolution( uis.glconfig.vidWidth, uis.glconfig.vidHeight, qtrue );

	qsort( resList, numResolutions, sizeof( resList[ 0 ] ), GraphicsOptions_CompareResolutions );

	numRatios = 0;
	for ( i = 0; i < numResolutions; i++ ) {
		char name[ 8 ];

		Com_sprintf( resList[ i ].name, sizeof( resList[ i ].name ), "%dx%d", resList[ i ].w, resList[ i ].h );
		resolutions[ i ] = resList[ i ].name;

		GraphicsOptions_RatioName( resList[ i ].aspect, name, sizeof( name ) );
		for ( j = 0; j < numRatios; j++ ) {
			if ( !strcmp( ratioNames[ j ], name ) )
				break;
		}
		if ( j == numRatios ) {
			Q_strncpyz( ratioNames[ j ], name, sizeof( ratioNames[ j ] ) );
			ratios[ j ] = ratioNames[ j ];
			numRatios++;
		}
		resToRatio[ i ] = j;
		ratioToRes[ j ] = i;	// sorted by size: the last one is the largest
	}
	resolutions[ numResolutions ] = NULL;
	ratios[ numRatios ] = NULL;
}

/*
=================
GraphicsOptions_GetInitialVideo/*
=================
GraphicsOptions_GetInitialVideo
=================
*/
static void GraphicsOptions_GetInitialVideo( void )
{
	s_ivo.driver      = s_graphicsoptions.driver.curvalue;
	s_ivo.mode        = s_graphicsoptions.mode.curvalue;
	s_ivo.fullscreen  = s_graphicsoptions.fs.curvalue;
	s_ivo.extensions  = s_graphicsoptions.allow_extensions.curvalue;
	s_ivo.tq          = s_graphicsoptions.tq.curvalue;
	s_ivo.lighting    = s_graphicsoptions.lighting.curvalue;
	s_ivo.flares      = s_graphicsoptions.flares.curvalue;
	s_ivo.bloom      = s_graphicsoptions.bloom.curvalue;
	s_ivo.drawfps     = s_graphicsoptions.drawfps.curvalue;
	s_ivo.geometry    = s_graphicsoptions.geometry.curvalue;
	s_ivo.filter      = s_graphicsoptions.filter.curvalue;
        s_ivo.aniso      = s_graphicsoptions.aniso.curvalue;
	s_ivo.texturebits = s_graphicsoptions.texturebits.curvalue;
}

/*
=================
GraphicsOptions_CheckConfig
=================
*/
static void GraphicsOptions_CheckConfig( void )
{
	int i;

	for ( i = 0; i < NUM_IVO_TEMPLATES-1; i++ )
	{
		if ( s_ivo_templates[i].driver != s_graphicsoptions.driver.curvalue )
			continue;
		if ( s_ivo_templates[i].fullscreen != s_graphicsoptions.fs.curvalue )
			continue;
		if ( s_ivo_templates[i].tq != s_graphicsoptions.tq.curvalue )
			continue;
		if ( s_ivo_templates[i].lighting != s_graphicsoptions.lighting.curvalue )
			continue;
                if ( s_ivo_templates[i].flares != s_graphicsoptions.flares.curvalue )
			continue;
                if ( s_ivo_templates[i].bloom != s_graphicsoptions.bloom.curvalue )
			continue;
		if ( s_ivo_templates[i].drawfps != s_graphicsoptions.drawfps.curvalue )
			continue;
		if ( s_ivo_templates[i].geometry != s_graphicsoptions.geometry.curvalue )
			continue;
		if ( s_ivo_templates[i].filter != s_graphicsoptions.filter.curvalue )
			continue;
                if ( s_ivo_templates[i].aniso != s_graphicsoptions.aniso.curvalue )
			continue;
//		if ( s_ivo_templates[i].texturebits != s_graphicsoptions.texturebits.curvalue )
//			continue;
		s_graphicsoptions.list.curvalue = i;
		return;
	}

	// return 'Custom' ivo template
	s_graphicsoptions.list.curvalue = NUM_IVO_TEMPLATES - 1;
}

/*
=================
GraphicsOptions_UpdateMenuItems
=================
*/
static void GraphicsOptions_UpdateMenuItems( void )
{
	if ( s_graphicsoptions.driver.curvalue == 1 )
	{
		s_graphicsoptions.fs.curvalue = 1;
		s_graphicsoptions.fs.generic.flags |= QMF_GRAYED;
	}
	else
	{
		s_graphicsoptions.fs.generic.flags &= ~QMF_GRAYED;
	}

	if ( s_graphicsoptions.allow_extensions.curvalue == 0 )
	{
		if ( s_graphicsoptions.texturebits.curvalue == 0 )
		{
			s_graphicsoptions.texturebits.curvalue = 1;
		}
	}

	s_graphicsoptions.apply.generic.flags |= QMF_HIDDEN|QMF_INACTIVE;

	if ( s_ivo.mode != s_graphicsoptions.mode.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.fullscreen != s_graphicsoptions.fs.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.extensions != s_graphicsoptions.allow_extensions.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.tq != s_graphicsoptions.tq.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.lighting != s_graphicsoptions.lighting.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
        if ( s_ivo.flares != s_graphicsoptions.flares.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
        if ( s_ivo.bloom != s_graphicsoptions.bloom.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.drawfps != s_graphicsoptions.drawfps.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.driver != s_graphicsoptions.driver.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.texturebits != s_graphicsoptions.texturebits.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.geometry != s_graphicsoptions.geometry.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
	if ( s_ivo.filter != s_graphicsoptions.filter.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}
        if ( s_ivo.aniso != s_graphicsoptions.aniso.curvalue )
	{
		s_graphicsoptions.apply.generic.flags &= ~(QMF_HIDDEN|QMF_INACTIVE);
	}

	GraphicsOptions_CheckConfig();
}	

/*
=================
GraphicsOptions_ApplyChanges
=================
*/
static void GraphicsOptions_ApplyChanges( void *unused, int notification )
{
	if (notification != QM_ACTIVATED)
		return;

	switch ( s_graphicsoptions.texturebits.curvalue  )
	{
	case 0:
		trap_Cvar_SetValue( "r_texturebits", 0 );
		break;
	case 1:
		trap_Cvar_SetValue( "r_texturebits", 16 );
		break;
	case 2:
		trap_Cvar_SetValue( "r_texturebits", 32 );
		break;
	}
	trap_Cvar_SetValue( "r_picmip", 3 - s_graphicsoptions.tq.curvalue );
	trap_Cvar_SetValue( "r_allowExtensions", s_graphicsoptions.allow_extensions.curvalue );

	// any listed size, as a custom mode
	if ( s_graphicsoptions.mode.curvalue >= 0 && s_graphicsoptions.mode.curvalue < numResolutions )
	{
		trap_Cvar_SetValue( "r_customwidth", resList[ s_graphicsoptions.mode.curvalue ].w );
		trap_Cvar_SetValue( "r_customheight", resList[ s_graphicsoptions.mode.curvalue ].h );
		trap_Cvar_SetValue( "r_mode", -1 );
	}

	trap_Cvar_SetValue( "r_fullscreen", s_graphicsoptions.fs.curvalue );
	trap_Cvar_SetValue( "r_colorbits", 0 );
	trap_Cvar_SetValue( "r_depthbits", 0 );
	trap_Cvar_SetValue( "r_stencilbits", 0 );
	trap_Cvar_SetValue( "r_vertexLight", s_graphicsoptions.lighting.curvalue );
	trap_Cvar_SetValue( "cg_autovertex", s_graphicsoptions.lighting.curvalue );
	trap_Cvar_SetValue( "r_flares", s_graphicsoptions.flares.curvalue );
	trap_Cvar_SetValue( "r_bloom", s_graphicsoptions.bloom.curvalue );
	trap_Cvar_SetValue( "cg_drawFPS", s_graphicsoptions.drawfps.curvalue );

	//r_ext_texture_filter_anisotropic is special
	if(s_graphicsoptions.aniso.curvalue) {
		trap_Cvar_SetValue( "r_ext_max_anisotropy", s_graphicsoptions.aniso.curvalue*2 );
		trap_Cvar_SetValue( "r_ext_texture_filter_anisotropic", qtrue );
	}
	else {
		trap_Cvar_SetValue( "r_ext_texture_filter_anisotropic", qfalse );
	}

	if ( s_graphicsoptions.geometry.curvalue == 2 )
	{
		trap_Cvar_SetValue( "r_lodBias", 0 );
		trap_Cvar_SetValue( "r_subdivisions", 4 );
	}
	else if ( s_graphicsoptions.geometry.curvalue == 1 )
	{
		trap_Cvar_SetValue( "r_lodBias", 1 );
		trap_Cvar_SetValue( "r_subdivisions", 12 );
	}
	else
	{
		trap_Cvar_SetValue( "r_lodBias", 1 );
		trap_Cvar_SetValue( "r_subdivisions", 20 );
	}

	if ( s_graphicsoptions.filter.curvalue )
	{
		trap_Cvar_Set( "r_textureMode", "GL_LINEAR_MIPMAP_LINEAR" );
	}
	else
	{
		trap_Cvar_Set( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST" );
	}

	trap_Cmd_ExecuteText( EXEC_APPEND, "vid_restart\n" );
}

/*
=================
GraphicsOptions_Event
=================
*/
static void GraphicsOptions_Event( void* ptr, int event ) {
	InitialVideoOptions_s *ivo;

	if( event != QM_ACTIVATED ) {
	 	return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
        case ID_RATIO:
		s_graphicsoptions.mode.curvalue = ratioToRes[ s_graphicsoptions.ratio.curvalue ];
		break;

	case ID_MODE:
		s_graphicsoptions.ratio.curvalue = resToRatio[ s_graphicsoptions.mode.curvalue ];
		break;

	case ID_LIST:
		ivo = &s_ivo_templates[s_graphicsoptions.list.curvalue];

		// a quality preset leaves the resolution alone
		s_graphicsoptions.tq.curvalue          = ivo->tq;
		s_graphicsoptions.lighting.curvalue    = ivo->lighting;
		s_graphicsoptions.texturebits.curvalue = ivo->texturebits;
		s_graphicsoptions.geometry.curvalue    = ivo->geometry;
		s_graphicsoptions.filter.curvalue      = ivo->filter;
                s_graphicsoptions.aniso.curvalue       = ivo->aniso;
		s_graphicsoptions.fs.curvalue          = ivo->fullscreen;
                s_graphicsoptions.flares.curvalue      = ivo->flares;
                s_graphicsoptions.bloom.curvalue      = ivo->bloom;
                s_graphicsoptions.drawfps.curvalue      = ivo->drawfps;
		break;

	case ID_DRIVERINFO:
		UI_DriverInfo_Menu();
		break;

	case ID_BACK2:
		UI_PopMenu();
		break;

	case ID_GRAPHICS:
		break;

	case ID_DISPLAY:
		UI_PopMenu();
		UI_DisplayOptionsMenu();
		break;

	case ID_SOUND:
		UI_PopMenu();
		UI_SoundOptionsMenu();
		break;

	case ID_NETWORK:
		UI_PopMenu();
		UI_NetworkOptionsMenu();
		break;
	}
}


/*
================
GraphicsOptions_TQEvent
================
*/
static void GraphicsOptions_TQEvent( void *ptr, int event ) {
	if( event != QM_ACTIVATED ) {
	 	return;
	}
	s_graphicsoptions.tq.curvalue = (int)(s_graphicsoptions.tq.curvalue + 0.5);
}


/*
================
GraphicsOptions_MenuDraw
================
*/
void GraphicsOptions_MenuDraw (void)
{
//APSFIX - rework this
	GraphicsOptions_UpdateMenuItems();

	Menu_Draw( &s_graphicsoptions.menu );
}

/*
=================
GraphicsOptions_SetMenuItems
=================
*/
static void GraphicsOptions_SetMenuItems( void )
{
	// the size the game runs at now
	s_graphicsoptions.mode.curvalue = GraphicsOptions_FindResolution( uis.glconfig.vidWidth, uis.glconfig.vidHeight );
	if ( s_graphicsoptions.mode.curvalue < 0 )
		s_graphicsoptions.mode.curvalue = 0;
	s_graphicsoptions.ratio.curvalue = resToRatio[ s_graphicsoptions.mode.curvalue ];
	s_graphicsoptions.fs.curvalue = trap_Cvar_VariableValue("r_fullscreen");
	s_graphicsoptions.allow_extensions.curvalue = trap_Cvar_VariableValue("r_allowExtensions");
        s_graphicsoptions.flares.curvalue = trap_Cvar_VariableValue("r_flares");
        s_graphicsoptions.bloom.curvalue = trap_Cvar_VariableValue("r_bloom");
        s_graphicsoptions.drawfps.curvalue = trap_Cvar_VariableValue("cg_drawFPS");
        if(trap_Cvar_VariableValue("r_ext_texture_filter_anisotropic")) {
            s_graphicsoptions.aniso.curvalue = trap_Cvar_VariableValue("r_ext_max_anisotropy")/2;
        }
	s_graphicsoptions.tq.curvalue = 3-trap_Cvar_VariableValue( "r_picmip");
	if ( s_graphicsoptions.tq.curvalue < 0 )
	{
		s_graphicsoptions.tq.curvalue = 0;
	}
	else if ( s_graphicsoptions.tq.curvalue > 3 )
	{
		s_graphicsoptions.tq.curvalue = 3;
	}

	s_graphicsoptions.lighting.curvalue = trap_Cvar_VariableValue( "r_vertexLight" ) != 0;
	switch ( ( int ) trap_Cvar_VariableValue( "r_texturebits" ) )
	{
	default:
	case 0:
		s_graphicsoptions.texturebits.curvalue = 0;
		break;
	case 16:
		s_graphicsoptions.texturebits.curvalue = 1;
		break;
	case 32:
		s_graphicsoptions.texturebits.curvalue = 2;
		break;
	}

	if ( Q_strequal( UI_Cvar_VariableString( "r_textureMode" ), "GL_LINEAR_MIPMAP_NEAREST" ) )
	{
		s_graphicsoptions.filter.curvalue = 0;
	}
	else
	{
		s_graphicsoptions.filter.curvalue = 1;
	}

	if ( trap_Cvar_VariableValue( "r_lodBias" ) > 0 )
	{
		if ( trap_Cvar_VariableValue( "r_subdivisions" ) >= 20 )
		{
			s_graphicsoptions.geometry.curvalue = 0;
		}
		else
		{
			s_graphicsoptions.geometry.curvalue = 1;
		}
	}
	else
	{
		s_graphicsoptions.geometry.curvalue = 2;
	}
}

/*
================
GraphicsOptions_MenuInit
================
*/
void GraphicsOptions_MenuInit( void )
{
	static const char *s_driver_names[] =
	{
		"Default",
		"Voodoo",
		NULL
	};

	static const char *tq_names[] =
	{
		"Default",
		"16 bit",
		"32 bit",
		NULL
	};

	static const char *s_graphics_options_names[] =
	{
		"Very High Quality",
		"High Quality",
		"Normal",
		"Fast",
		"Fastest",
		"Custom",
		NULL
	};

	static const char *lighting_names[] =
	{
		"Lightmap (Normal)",
		"Vertex (Low)",
		NULL
	};


	static const char *filter_names[] =
	{
		"Bilinear",
		"Trilinear",
		NULL
	};
        
        static const char *aniso_names[] =
	{
		"Off",
		"2x",
                "4x",
                "6x",
                "8x",
		NULL
	};
        
	static const char *quality_names[] =
	{
		"Low",
		"Medium",
		"High",
		NULL
	};
	static const char *enabled_names[] =
	{
		"Off",
		"On",
		NULL
	};

	int y;

	// zero set all our globals
	memset( &s_graphicsoptions, 0 ,sizeof(graphicsoptions_t) );


	GraphicsOptions_GetResolutions();

	GraphicsOptions_Cache();

	s_graphicsoptions.menu.wrapAround = qtrue;
	s_graphicsoptions.menu.fullscreen = qtrue;
	s_graphicsoptions.menu.draw       = GraphicsOptions_MenuDraw;

	s_graphicsoptions.banner.generic.type  = MTYPE_BTEXT;
	s_graphicsoptions.banner.generic.x	   = 320;
	s_graphicsoptions.banner.generic.y	   = 16;
	s_graphicsoptions.banner.string  	   = "SYSTEM SETUP";
	s_graphicsoptions.banner.color         = color_white;
	s_graphicsoptions.banner.style         = UI_CENTER;

	s_graphicsoptions.framel.generic.type  = MTYPE_BITMAP;
	s_graphicsoptions.framel.generic.name  = GRAPHICSOPTIONS_FRAMEL;
	s_graphicsoptions.framel.generic.flags = QMF_INACTIVE;
	s_graphicsoptions.framel.generic.x	   = 0;
	s_graphicsoptions.framel.generic.y	   = 78;
	s_graphicsoptions.framel.width  	   = 256;
	s_graphicsoptions.framel.height  	   = 329;

	s_graphicsoptions.framer.generic.type  = MTYPE_BITMAP;
	s_graphicsoptions.framer.generic.name  = GRAPHICSOPTIONS_FRAMER;
	s_graphicsoptions.framer.generic.flags = QMF_INACTIVE;
	s_graphicsoptions.framer.generic.x	   = 376;
	s_graphicsoptions.framer.generic.y	   = 76;
	s_graphicsoptions.framer.width  	   = 256;
	s_graphicsoptions.framer.height  	   = 334;

	s_graphicsoptions.graphics.generic.type		= MTYPE_PTEXT;
	s_graphicsoptions.graphics.generic.flags	= QMF_RIGHT_JUSTIFY;
	s_graphicsoptions.graphics.generic.id		= ID_GRAPHICS;
	s_graphicsoptions.graphics.generic.callback	= GraphicsOptions_Event;
	s_graphicsoptions.graphics.generic.x		= 216;
	s_graphicsoptions.graphics.generic.y		= 240 - 2 * PROP_HEIGHT;
	s_graphicsoptions.graphics.string			= "GRAPHICS";
	s_graphicsoptions.graphics.style			= UI_RIGHT;
	s_graphicsoptions.graphics.color			= color_red;

	s_graphicsoptions.display.generic.type		= MTYPE_PTEXT;
	s_graphicsoptions.display.generic.flags		= QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_graphicsoptions.display.generic.id		= ID_DISPLAY;
	s_graphicsoptions.display.generic.callback	= GraphicsOptions_Event;
	s_graphicsoptions.display.generic.x			= 216;
	s_graphicsoptions.display.generic.y			= 240 - PROP_HEIGHT;
	s_graphicsoptions.display.string			= "DISPLAY";
	s_graphicsoptions.display.style				= UI_RIGHT;
	s_graphicsoptions.display.color				= color_red;

	s_graphicsoptions.sound.generic.type		= MTYPE_PTEXT;
	s_graphicsoptions.sound.generic.flags		= QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_graphicsoptions.sound.generic.id			= ID_SOUND;
	s_graphicsoptions.sound.generic.callback	= GraphicsOptions_Event;
	s_graphicsoptions.sound.generic.x			= 216;
	s_graphicsoptions.sound.generic.y			= 240;
	s_graphicsoptions.sound.string				= "SOUND";
	s_graphicsoptions.sound.style				= UI_RIGHT;
	s_graphicsoptions.sound.color				= color_red;

	s_graphicsoptions.network.generic.type		= MTYPE_PTEXT;
	s_graphicsoptions.network.generic.flags		= QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_graphicsoptions.network.generic.id		= ID_NETWORK;
	s_graphicsoptions.network.generic.callback	= GraphicsOptions_Event;
	s_graphicsoptions.network.generic.x			= 216;
	s_graphicsoptions.network.generic.y			= 240 + PROP_HEIGHT;
	s_graphicsoptions.network.string			= "NETWORK";
	s_graphicsoptions.network.style				= UI_RIGHT;
	s_graphicsoptions.network.color				= color_red;

	y = 240 - 7 * (BIGCHAR_HEIGHT + 2);
	s_graphicsoptions.list.generic.type     = MTYPE_SPINCONTROL;
	s_graphicsoptions.list.generic.name     = "Graphics Settings:";
	s_graphicsoptions.list.generic.flags    = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.list.generic.x        = 400;
	s_graphicsoptions.list.generic.y        = y;
	s_graphicsoptions.list.generic.callback = GraphicsOptions_Event;
	s_graphicsoptions.list.generic.id       = ID_LIST;
	s_graphicsoptions.list.itemnames        = s_graphics_options_names;
	y += 2 * ( BIGCHAR_HEIGHT + 2 );

	s_graphicsoptions.driver.generic.type  = MTYPE_SPINCONTROL;
	s_graphicsoptions.driver.generic.name  = "GL Driver:";
	s_graphicsoptions.driver.generic.flags = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.driver.generic.x     = 400;
	s_graphicsoptions.driver.generic.y     = y;
	s_graphicsoptions.driver.itemnames     = s_driver_names;
	s_graphicsoptions.driver.curvalue      = (uis.glconfig.driverType == GLDRV_VOODOO);
	y += BIGCHAR_HEIGHT+2;

	// references/modifies "r_allowExtensions"
	s_graphicsoptions.allow_extensions.generic.type     = MTYPE_SPINCONTROL;
	s_graphicsoptions.allow_extensions.generic.name	    = "GL Extensions:";
	s_graphicsoptions.allow_extensions.generic.flags	= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.allow_extensions.generic.x	    = 400;
	s_graphicsoptions.allow_extensions.generic.y	    = y;
	s_graphicsoptions.allow_extensions.itemnames        = enabled_names;
	y += BIGCHAR_HEIGHT+2;

        s_graphicsoptions.ratio.generic.type     = MTYPE_SPINCONTROL;
        s_graphicsoptions.ratio.generic.name     = "Aspect Ratio:";
        s_graphicsoptions.ratio.generic.flags    = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
        s_graphicsoptions.ratio.generic.x        = 400;
        s_graphicsoptions.ratio.generic.y        = y;
        s_graphicsoptions.ratio.itemnames        = ratios;
        s_graphicsoptions.ratio.generic.callback = GraphicsOptions_Event;
        s_graphicsoptions.ratio.generic.id       = ID_RATIO;
        y += BIGCHAR_HEIGHT+2;


	// references/modifies "r_mode"
	s_graphicsoptions.mode.generic.type     = MTYPE_SPINCONTROL;
	s_graphicsoptions.mode.generic.name     = "Resolution:";
	s_graphicsoptions.mode.generic.flags    = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.mode.generic.x        = 400;
	s_graphicsoptions.mode.generic.y        = y;
	s_graphicsoptions.mode.itemnames        = resolutions;
	s_graphicsoptions.mode.generic.callback = GraphicsOptions_Event;
	s_graphicsoptions.mode.generic.id       = ID_MODE;
	y += BIGCHAR_HEIGHT+2;

	// references/modifies "r_fullscreen"
	s_graphicsoptions.fs.generic.type     = MTYPE_SPINCONTROL;
	s_graphicsoptions.fs.generic.name	  = "Fullscreen:";
	s_graphicsoptions.fs.generic.flags	  = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.fs.generic.x	      = 400;
	s_graphicsoptions.fs.generic.y	      = y;
	s_graphicsoptions.fs.itemnames	      = enabled_names;
	y += BIGCHAR_HEIGHT+2;

	// references/modifies "r_vertexLight"
	s_graphicsoptions.lighting.generic.type  = MTYPE_SPINCONTROL;
	s_graphicsoptions.lighting.generic.name	 = "Lighting:";
	s_graphicsoptions.lighting.generic.flags = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.lighting.generic.x	 = 400;
	s_graphicsoptions.lighting.generic.y	 = y;
	s_graphicsoptions.lighting.itemnames     = lighting_names;
	y += BIGCHAR_HEIGHT+2;
        
        // references/modifies "r_flares"
	s_graphicsoptions.flares.generic.type     = MTYPE_SPINCONTROL;
	s_graphicsoptions.flares.generic.name	  = "Flares:";
	s_graphicsoptions.flares.generic.flags	  = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.flares.generic.x	      = 400;
	s_graphicsoptions.flares.generic.y	      = y;
	s_graphicsoptions.flares.itemnames	      = enabled_names;
	y += BIGCHAR_HEIGHT+2;
        
        // references/modifies "r_bloom"
	s_graphicsoptions.bloom.generic.type     = MTYPE_SPINCONTROL;
	s_graphicsoptions.bloom.generic.name	  = "Bloom:";
	s_graphicsoptions.bloom.generic.flags	  = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.bloom.generic.x	      = 400;
	s_graphicsoptions.bloom.generic.y	      = y;
	s_graphicsoptions.bloom.itemnames	      = enabled_names;
	y += BIGCHAR_HEIGHT+2;

	s_graphicsoptions.drawfps.generic.type	  = MTYPE_SPINCONTROL;
	s_graphicsoptions.drawfps.generic.name	  = "Draw FPS:";
	s_graphicsoptions.drawfps.generic.flags	  = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.drawfps.generic.x	      = 400;
	s_graphicsoptions.drawfps.generic.y	      = y;
	s_graphicsoptions.drawfps.itemnames	      = enabled_names;
	y += BIGCHAR_HEIGHT+2;

	// references/modifies "r_lodBias" & "subdivisions"
	s_graphicsoptions.geometry.generic.type  = MTYPE_SPINCONTROL;
	s_graphicsoptions.geometry.generic.name	 = "Geometric Detail:";
	s_graphicsoptions.geometry.generic.flags = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.geometry.generic.x	 = 400;
	s_graphicsoptions.geometry.generic.y	 = y;
	s_graphicsoptions.geometry.itemnames     = quality_names;
	y += BIGCHAR_HEIGHT+2;

	// references/modifies "r_picmip"
	s_graphicsoptions.tq.generic.type	= MTYPE_SLIDER;
	s_graphicsoptions.tq.generic.name	= "Texture Detail:";
	s_graphicsoptions.tq.generic.flags	= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.tq.generic.x		= 400;
	s_graphicsoptions.tq.generic.y		= y;
	s_graphicsoptions.tq.minvalue       = 0;
	s_graphicsoptions.tq.maxvalue       = 3;
	s_graphicsoptions.tq.generic.callback = GraphicsOptions_TQEvent;
	y += BIGCHAR_HEIGHT+2;

	// references/modifies "r_textureBits"
	s_graphicsoptions.texturebits.generic.type  = MTYPE_SPINCONTROL;
	s_graphicsoptions.texturebits.generic.name	= "Texture Quality:";
	s_graphicsoptions.texturebits.generic.flags	= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.texturebits.generic.x	    = 400;
	s_graphicsoptions.texturebits.generic.y	    = y;
	s_graphicsoptions.texturebits.itemnames     = tq_names;
	y += BIGCHAR_HEIGHT+2;

	// references/modifies "r_textureMode"
	s_graphicsoptions.filter.generic.type   = MTYPE_SPINCONTROL;
	s_graphicsoptions.filter.generic.name	= "Texture Filter:";
	s_graphicsoptions.filter.generic.flags	= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.filter.generic.x	    = 400;
	s_graphicsoptions.filter.generic.y	    = y;
	s_graphicsoptions.filter.itemnames      = filter_names;
	y += 2+BIGCHAR_HEIGHT;
        
        s_graphicsoptions.aniso.generic.type   = MTYPE_SPINCONTROL;
	s_graphicsoptions.aniso.generic.name	= "Anisotropy:";
	s_graphicsoptions.aniso.generic.flags	= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_graphicsoptions.aniso.generic.x	    = 400;
	s_graphicsoptions.aniso.generic.y	    = y;
	s_graphicsoptions.aniso.itemnames      = aniso_names;
	y += 2*BIGCHAR_HEIGHT;

	s_graphicsoptions.driverinfo.generic.type     = MTYPE_PTEXT;
	s_graphicsoptions.driverinfo.generic.flags    = QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_graphicsoptions.driverinfo.generic.callback = GraphicsOptions_Event;
	s_graphicsoptions.driverinfo.generic.id       = ID_DRIVERINFO;
	s_graphicsoptions.driverinfo.generic.x        = 320;
	s_graphicsoptions.driverinfo.generic.y        = y;
	s_graphicsoptions.driverinfo.string           = "Driver Info";
	s_graphicsoptions.driverinfo.style            = UI_CENTER|UI_SMALLFONT;
	s_graphicsoptions.driverinfo.color            = color_red;
	y += BIGCHAR_HEIGHT+2;

	s_graphicsoptions.back.generic.type	    = MTYPE_BITMAP;
	s_graphicsoptions.back.generic.name     = GRAPHICSOPTIONS_BACK0;
	s_graphicsoptions.back.generic.flags    = QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_graphicsoptions.back.generic.callback = GraphicsOptions_Event;
	s_graphicsoptions.back.generic.id	    = ID_BACK2;
	s_graphicsoptions.back.generic.x		= 0;
	s_graphicsoptions.back.generic.y		= 480-64;
	s_graphicsoptions.back.width  		    = 128;
	s_graphicsoptions.back.height  		    = 64;
	s_graphicsoptions.back.focuspic         = GRAPHICSOPTIONS_BACK1;

	s_graphicsoptions.apply.generic.type     = MTYPE_BITMAP;
	s_graphicsoptions.apply.generic.name     = GRAPHICSOPTIONS_ACCEPT0;
	s_graphicsoptions.apply.generic.flags    = QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS|QMF_HIDDEN|QMF_INACTIVE;
	s_graphicsoptions.apply.generic.callback = GraphicsOptions_ApplyChanges;
	s_graphicsoptions.apply.generic.x        = 640;
	s_graphicsoptions.apply.generic.y        = 480-64;
	s_graphicsoptions.apply.width  		     = 128;
	s_graphicsoptions.apply.height  		 = 64;
	s_graphicsoptions.apply.focuspic         = GRAPHICSOPTIONS_ACCEPT1;

	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.banner );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.framel );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.framer );

	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.graphics );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.display );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.sound );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.network );

	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.list );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.driver );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.allow_extensions );
        Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.ratio );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.mode );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.fs );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.lighting );
        Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.flares );
        Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.bloom );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.drawfps );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.geometry );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.tq );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.texturebits );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.filter );
        Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.aniso );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.driverinfo );

	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.back );
	Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.apply );

	GraphicsOptions_SetMenuItems();
	GraphicsOptions_GetInitialVideo();

	if ( uis.glconfig.driverType == GLDRV_ICD &&
		 uis.glconfig.hardwareType == GLHW_3DFX_2D3D )
	{
		s_graphicsoptions.driver.generic.flags |= QMF_HIDDEN|QMF_INACTIVE;
	}
}


/*
=================
GraphicsOptions_Cache
=================
*/
void GraphicsOptions_Cache( void ) {
	trap_R_RegisterShaderNoMip( GRAPHICSOPTIONS_FRAMEL );
	trap_R_RegisterShaderNoMip( GRAPHICSOPTIONS_FRAMER );
	trap_R_RegisterShaderNoMip( GRAPHICSOPTIONS_BACK0 );
	trap_R_RegisterShaderNoMip( GRAPHICSOPTIONS_BACK1 );
	trap_R_RegisterShaderNoMip( GRAPHICSOPTIONS_ACCEPT0 );
	trap_R_RegisterShaderNoMip( GRAPHICSOPTIONS_ACCEPT1 );
}


/*
=================
UI_GraphicsOptionsMenu
=================
*/
void UI_GraphicsOptionsMenu( void ) {
	GraphicsOptions_MenuInit();
	UI_PushMenu( &s_graphicsoptions.menu );
	Menu_SetCursorToItem( &s_graphicsoptions.menu, &s_graphicsoptions.graphics );
}
