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
#define G_OAX_SCRIPT_BASE   1010  /* 1010-1039 map scripting (oa-engine idscript/oax_script.h) */
#define G_OAX_SCRIPT_INIT           1010  /* ( int randomSeed ) -> 1 */
#define G_OAX_SCRIPT_REGISTER_EVENT 1011  /* ( name, argfmt, int ret, int flags ) -> event number, -1 */
#define G_OAX_SCRIPT_COMPILE_FILE   1012  /* ( path ) -> 1 ok, 0 compile error, -1 missing */
#define G_OAX_SCRIPT_SET_ENTITY     1013  /* ( name, int handle ) -> 1 if scripts use $name */
#define G_OAX_SCRIPT_START_THREAD   1014  /* ( func, int self ) -> thread number, 0 */
#define G_OAX_SCRIPT_RUN            1015  /* ( int levelTime, oaxScriptCall_t *call ) -> 0 done, 1 call pending */
#define G_OAX_SCRIPT_RETURN         1016  /* ( const oaxScriptValue_t *value, const char *string ) */
#define G_OAX_SCRIPT_OBJECT_DONE    1017  /* ( int threadNum, int handle ) */
#define G_OAX_SCRIPT_KILL_THREAD    1018  /* ( int threadNum ) */
#define G_OAX_SCRIPT_SHUTDOWN       1019  /* ( void ) */
#define G_OAX_SCRIPT_NUM_THREADS    1020  /* ( void ) -> live threads */
#define G_OAX_GUI_BASE      1040  /* 1040-1059 in-world GUIs */
#define G_OAX_GUI_LOAD          1040  /* ( const char *guiFile ) -> handle, 0 on failure */
#define G_OAX_GUI_FREE          1041  /* ( int handle ) */
#define G_OAX_GUI_SETSTATE      1042  /* ( int handle, const char *key, const char *value ) */
#define G_OAX_GUI_GETSTATE      1043  /* ( int handle, const char *key, char *buf, int size ) -> found */
#define G_OAX_GUI_HANDLE_EVENT  1044  /* ( int handle, float x, float y, int buttons, int time, char *cmds, int size ) -> length */
#define G_OAX_GUI_TRACE         1045  /* ( int entnum, const vec3_t start, const vec3_t end, float *xyFrac ) -> hit */
#define G_OAX_GUI_ACTIVATE      1046  /* ( int handle, int activate, int time, char *cmds, int size ) -> length */
#define G_OAX_GUI_NAMED_EVENT   1047  /* ( int handle, const char *name, int time, char *cmds, int size ) -> length */
#define G_OAX_GUI_STATE_INFO    1048  /* ( int handle, char *buf, int size ) -> length */
#define G_OAX_PORTAL_BASE   1060  /* 1060-1069 area portals, world queries */
#define G_OAX_ULIGHT_BASE   1070  /* 1070-1079 unified lighting */

/* cgame imports */
#define CG_OAX_DEBUG_SET    1000
#define CG_OAX_BSPX_READ    1001
#define CG_OAX_R_BASE       1010  /* 1010-1019 light styles, view fog, sky portal */
#define CG_OAX_R_SETLIGHTSTYLE 1010  /* ( int style, float r, float g, float b ) token "lightstyle" */
#define CG_OAX_R_SETVIEWFOG    1012  /* ( const float *rgb, float density, float start, float end ) token "viewfog";
                                        density 0 = off; end > start: linear to `density` (0..1),
                                        else 1 - exp(-density * (dist - start)) */
/* effects (phase 6), tokens "particles", "decals", "trails" (engine renderergl2 tr_oax_fx*.c) */
#define CG_OAX_R_REGISTERFX    1013  /* ( const char *particleDecl ) -> handle, 0 if none */
#define CG_OAX_R_ADDFX         1014  /* ( const oaxFx_t *fx ) -> 1 while alive or to come, 0 once done */
#define CG_OAX_R_ADDDECAL      1015  /* ( const oaxDecal_t *decal ) -> polygons projected (0 none or off) */
#define CG_OAX_R_ADDTRAIL      1016  /* ( const oaxTrail_t *trail, const float *points ): x y z ageMs each, newest first */
#define CG_OAX_R_CLEARDECALS   1017  /* ( void ) */
#define CG_OAX_S_BASE       1020  /* 1020-1029 zone reverb, occlusion */
#define CG_OAX_S_SETREVERB  1020  /* ( const char *preset, float decay, float wet ) token "reverb" */
#define CG_OAX_GUI_BASE     1030  /* 1030-1049 in-world GUIs */
#define CG_OAX_GUI_LOAD          1030  /* ( const char *guiFile ) -> handle, 0 on failure */
#define CG_OAX_GUI_FREE          1031  /* ( int handle ) */
#define CG_OAX_GUI_SETSTATE      1032  /* ( int handle, const char *key, const char *value ) */
#define CG_OAX_GUI_ACTIVATE      1033  /* ( int handle, int activate ) */
#define CG_OAX_R_ADDREFENTITYEXT 1034  /* ( const refEntity_t *re, const refEntityExt_t *ext ) */
#define CG_OAX_GUI_TRACE         1035  /* ( int inlineModel, origin, angles, start, end, float *xyFrac ) -> hit */
#define CG_OAX_GUI_CURSOR        1036  /* ( int handle, float x, float y ): cosmetic hover */


/* physics, game AND cgame: one block, same numbers (structs in oax_phys.h,
   traps in bg_oax_phys.h); token "physics", skeleton calls "physics_skel" */
#define OAX_PHYS_BASE               1200
#define PHYS_WORLD_CREATE           1200
#define PHYS_WORLD_DESTROY          1201
#define PHYS_WORLD_STEP             1202
#define PHYS_WORLD_ADD_BSP          1203
#define PHYS_WORLD_ADD_HEIGHTFIELD  1204
#define PHYS_WORLD_SET_GRAVITY      1205
#define PHYS_WORLD_STATS            1206
#define PHYS_WORLD_HASH             1207
#define PHYS_WORLD_EXPLODE          1208
#define PHYS_WORLD_CONTACT_EVENTS   1209
#define PHYS_BODY_CREATE            1210
#define PHYS_BODY_DESTROY           1211
#define PHYS_BODY_ADD_SHAPE         1212
#define PHYS_BODY_SET_TRANSFORM     1213
#define PHYS_BODY_SET_VELOCITY      1214
#define PHYS_BODY_APPLY             1215
#define PHYS_BODY_SET_TARGET        1216
#define PHYS_BODY_SET_PARAM         1217
#define PHYS_BODY_GET_STATE         1218
#define PHYS_BODY_GET_STATES        1219
#define PHYS_BODY_FROM_BSP_MODEL    1220
#define PHYS_BODY_GET_MASS          1221
#define PHYS_RAGDOLL_CREATE         1222
#define PHYS_JOINT_CREATE           1230
#define PHYS_JOINT_DESTROY          1231
#define PHYS_JOINT_SET_PARAM        1232
#define PHYS_JOINT_GET_PARAM        1233
#define PHYS_RAYCAST                1240
#define PHYS_RAYCAST_BATCH          1241
#define PHYS_SHAPECAST              1242
#define PHYS_OVERLAP                1243
#define PHYS_R_MODEL_SKELETON       1250  /* cgame only */
#define PHYS_R_LERP_SKELETON        1251  /* cgame only */
#define PHYS_R_ADD_SKELETAL_ENTITY  1252  /* cgame only */
#define PHYS_R_MODEL_FRAMES         1253  /* cgame only */

/* what a refEntity carries beyond the stock layout (engine tr_types.h) */
typedef struct {
	int		guiHandle;			/* client GUI on the entity's "map $gui" stages, 0 = none */
	float	shaderParms[12];	/* unified lighting (phase 5) */
	int		lightDefMask;		/* unified lighting (phase 5) */
} refEntityExt_t;
#define CG_OAX_ULIGHT_BASE  1050  /* 1050-1069 unified lighting */
#define CG_OAX_R_UPDATELIGHTDEF 1050  /* ( int lightOrdinal, const vec3_t origin, const vec3_t axis[3] or NULL,
                                         const vec3_t rgb, const float parms[12] or NULL, int flags: 1 on ) */

/* effects (engine tr_types.h has the same layouts) */
#define OAXFX_SHADERTIME    0x0001  /* times are on the shader clock (r_fixedShaderTime freezes it) */
typedef struct {
	int     handle;
	int     startTime;          /* ms */
	int     stopTime;           /* ms, 0 = never */
	int     seed;
	int     flags;              /* OAXFX_* */
	float   origin[3];
	float   axis[3][3];         /* axis[2] is the decl's up */
	float   rgba[4];            /* tint, 0 0 0 0 = white */
	float   scale;              /* 0 = 1 */
} oaxFx_t;

#define OAXDECAL_ALPHAFADE  0x0001  /* fade alpha, else rgb */
typedef struct {
	int     shader;
	float   origin[3];
	float   axis[3][3];         /* axis[0] out of the surface; axis[1], axis[2] texture s, t */
	float   halfSize[3];
	float   rgba[4];
	int     startTime;          /* ms (cg.time) */
	int     lifeMs;             /* 0 = until the cap replaces it */
	int     fadeMs;
	int     flags;              /* OAXDECAL_* */
} oaxDecal_t;

typedef struct {
	int     shader;
	int     numPoints;
	int     lifeMs;
	float   width[2];           /* at age 0, at lifeMs */
	float   rgba[2][4];
	float   texLength;          /* units per texture repeat, 0 = once over the trail */
	int     flags;
} oaxTrail_t;

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
#define CS_OAX_FXDECLS      1009  /* particle decl names of func_oax_emitter entities, space separated (index = s.generic1) */
#define CS_OAX_TRAILS       1010  /* "entnum shader width life r g b a;" for each entity that asks for a trail */

/*
 * Script VM call records (layout shared with the engine's oax_script.h;
 * never changes once shipped)
 */
#define OAX_SV_VOID         0
#define OAX_SV_FLOAT        1   /* f[0] */
#define OAX_SV_INT          2   /* i */
#define OAX_SV_VECTOR       3   /* f[0..2] */
#define OAX_SV_STRING       4   /* i = offset into strings[] */
#define OAX_SV_ENTITY       5   /* i = handle (entity number + 1), 0 = none */

#define OAX_SCRIPT_MAX_ARGS 8
#define OAX_SCRIPT_STRINGS  1024

typedef struct {
	int     type;
	int     i;
	float   f[3];
} oaxScriptValue_t;

typedef struct {
	int                 event;
	int                 self;
	int                 thread;
	int                 argc;
	oaxScriptValue_t    args[OAX_SCRIPT_MAX_ARGS];
	char                strings[OAX_SCRIPT_STRINGS];
} oaxScriptCall_t;

#define OAX_EVENT_ENTITY    2   /* $name.event( ... ) */
#define OAX_EVENT_SYS       4   /* sys.event( ... ) */
/* refdef flags for sky portals (token "skyportal"): the sky room scene,
   then the main scene drawn over it without its skyportal sky */
#define RDF_OAX_SKYPORTAL   0x0100
#define RDF_OAX_UNDERSKY    0x0200

/* light styles: CS_OAX_LIGHTSTYLES + (style - 32) holds "<on> <pattern> <rate>",
   pattern "-" when constant; letters a..z are 0..25/12 (m = 1) */
#define OAX_LIGHTSTYLE_FIRST  32
#define OAX_LIGHTSTYLE_COUNT  32

/* bg_oax.c */
int BG_OAXFeature( const char *token );   /* engine advertises token in oax_features */
void BG_OAXDebugSet( const char *name, const char *value );
void BG_OAXDebugSetInt( const char *name, int value );
void BG_OAXDebugSetFloat( const char *name, float value );

#endif
