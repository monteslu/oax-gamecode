/*
===========================================================================
oax_public.h: the oax engine extensions as gamecode sees them.

Mirrors oa-engine code/qcommon/oax.h: syscall numbers from 1000, which a
stock engine rejects, so every caller first checks BG_OAXFeature("<token>")
against the engine's read-only cvar oax_features. Numbers never change once
shipped; each feature owns a block.
===========================================================================
*/
#ifndef OAX_PUBLIC_H
#define OAX_PUBLIC_H

#define OAX_VERSION 1

/* game imports */
#define G_OAX_DEBUG_SET     1000  /* ( const char *name, const char *value ) */
#define G_OAX_BSPX_READ     1001  /* ( const char *lump, void *buf, int size ) -> length, -1 absent */
#define G_OAX_SCRIPT_BASE   1010  /* 1010-1039 map scripting */
#define G_OAX_GUI_BASE      1040  /* 1040-1059 in-world GUIs */
#define G_OAX_PORTAL_BASE   1060  /* 1060-1069 area portals, world queries */
#define G_OAX_ULIGHT_BASE   1070  /* 1070-1079 unified lighting */

/* cgame imports */
#define CG_OAX_DEBUG_SET    1000
#define CG_OAX_BSPX_READ    1001
#define CG_OAX_R_BASE       1010  /* 1010-1019 light styles, view fog, sky portal */
#define CG_OAX_S_BASE       1020  /* 1020-1029 zone reverb, occlusion */
#define CG_OAX_GUI_BASE     1030  /* 1030-1049 in-world GUIs */
#define CG_OAX_ULIGHT_BASE  1050  /* 1050-1069 unified lighting */

/*
 * Configstrings from CS_MAX (736) up belong to oax gamecode; the engine
 * never reads them. MAX_CONFIGSTRINGS is 1024.
 */
#define CS_OAX_INFO         736   /* map feature manifest, sky portal params */
#define CS_OAX_LIGHTSTYLES  737   /* 737-768: styles 32-63 */
#define CS_OAX_ZONES        769   /* 769-800: zone params */
#define CS_OAX_SPLINES      801   /* 801-864: spline control points */
#define CS_OAX_GUISTATE     865   /* 865-928: per-GUI state */
#define CS_OAX_SCRIPT       929   /* 929-944: script-driven state */
#define CS_OAX_ULIGHTS      945   /* 945-1008: controlled realtime lights */

/* bg_oax.c */
int BG_OAXFeature( const char *token );   /* engine advertises token in oax_features */
void BG_OAXDebugSet( const char *name, const char *value );
void BG_OAXDebugSetInt( const char *name, int value );
void BG_OAXDebugSetFloat( const char *name, float value );

#endif
