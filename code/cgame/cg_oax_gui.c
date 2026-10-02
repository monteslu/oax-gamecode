/*
===========================================================================
cg_oax_gui.c: in-world GUIs (func_oax_gui), the client side.

Each func_oax_gui has a slot in CS_OAX_GUISTATE: "e" (the entity), "f"
(the GUI file) and the server's state variables ("gui::<key>"). The client
loads its own copy of the GUI, applies that state as it changes, and adds
the panel entity with the GUI's handle so the renderer shows the GUI on
its "map $gui" face (the engine redraws the GUI into a texture first).

Focus and the cursor are cosmetic here: they follow the local aim so the
panel highlights what the player aims at, and a focused player's predicted
move does not fire. Clicks and state belong to the server (g_oax_gui.c).
===========================================================================
*/
#include "cg_local.h"

#define GUI_FOCUS_DIST		64
#define MAX_OAX_GUIS		64

typedef struct {
	int		entnum;
	int		handle;
	char	file[MAX_QPATH];
	char	cs[MAX_INFO_STRING];
} cgOaxGui_t;

static cgOaxGui_t	cgGuis[MAX_OAX_GUIS];
static int			cgGuiOn;
static int			cgGuiFocus;			/* 1 + slot */
static int			cgGuiUpdatedTime = -1;

int trap_OAX_CG_GuiLoad( const char *file );
void trap_OAX_CG_GuiFree( int handle );
void trap_OAX_CG_GuiSetState( int handle, const char *key, const char *value );
void trap_OAX_CG_GuiActivate( int handle, int activate );
void trap_OAX_R_AddRefEntityExt( const refEntity_t *re, const refEntityExt_t *ext );
int trap_OAX_CG_GuiTrace( int inlineModel, const vec3_t origin, const vec3_t angles, const vec3_t start, const vec3_t end, float *xyFrac );
void trap_OAX_CG_GuiCursor( int handle, float x, float y );

void CG_OAXGuiInit( void ) {
	memset( cgGuis, 0, sizeof( cgGuis ) );
	cgGuiFocus = 0;
	cgGuiUpdatedTime = -1;
	cgGuiOn = BG_OAXFeature( "gui" );
}

/* applies every "gui::" key of a slot's configstring to its GUI */
static void CG_OAXGuiApplyState( cgOaxGui_t *g ) {
	const char *s = g->cs;
	char key[MAX_INFO_KEY];
	char value[MAX_INFO_VALUE];

	while ( s && *s ) {
		Info_NextPair( &s, key, value );
		if ( !key[0] ) {
			break;
		}
		if ( !Q_strncmp( key, "gui::", 5 ) ) {
			trap_OAX_CG_GuiSetState( g->handle, key, value );
		}
	}
}

/*
=================
CG_OAXGuiUpdate

Picks up configstring changes (new GUIs, state), once per frame.
=================
*/
static void CG_OAXGuiUpdate( void ) {
	int i;

	if ( !cgGuiOn || cgGuiUpdatedTime == cg.time ) {
		return;
	}
	cgGuiUpdatedTime = cg.time;

	for ( i = 0; i < MAX_OAX_GUIS; i++ ) {
		cgOaxGui_t *g = &cgGuis[i];
		const char *cs = CG_ConfigString( CS_OAX_GUISTATE + i );
		const char *file;

		if ( !strcmp( cs, g->cs ) ) {
			continue;
		}
		Q_strncpyz( g->cs, cs, sizeof( g->cs ) );
		file = Info_ValueForKey( cs, "f" );
		if ( !file[0] ) {
			if ( g->handle ) {
				trap_OAX_CG_GuiFree( g->handle );
			}
			memset( g, 0, sizeof( *g ) );
			continue;
		}
		if ( Q_stricmp( file, g->file ) || !g->handle ) {
			if ( g->handle ) {
				trap_OAX_CG_GuiFree( g->handle );
			}
			Q_strncpyz( g->file, file, sizeof( g->file ) );
			g->handle = trap_OAX_CG_GuiLoad( g->file );
		}
		g->entnum = atoi( Info_ValueForKey( cs, "e" ) );
		if ( g->handle ) {
			CG_OAXGuiApplyState( g );
		}
	}
}

static cgOaxGui_t *CG_OAXGuiForEntity( int entnum ) {
	int i;

	for ( i = 0; i < MAX_OAX_GUIS; i++ ) {
		if ( cgGuis[i].handle && cgGuis[i].entnum == entnum ) {
			return &cgGuis[i];
		}
	}
	return NULL;
}

/*
=================
CG_OAXGuiAddEntity

From CG_Mover: adds a GUI panel with its GUI. qfalse when the entity has
no GUI (the caller adds it as usual).
=================
*/
qboolean CG_OAXGuiAddEntity( centity_t *cent, refEntity_t *ent ) {
	cgOaxGui_t *g;
	refEntityExt_t ext;

	if ( !cgGuiOn ) {
		return qfalse;
	}
	CG_OAXGuiUpdate();
	g = CG_OAXGuiForEntity( cent->currentState.number );
	if ( !g ) {
		return qfalse;
	}
	memset( &ext, 0, sizeof( ext ) );
	ext.guiHandle = g->handle;
	trap_OAX_R_AddRefEntityExt( ent, &ext );
	return qtrue;
}

/*
=================
CG_OAXGuiFrame

The local player's cosmetic focus and cursor, from the predicted aim.
=================
*/
void CG_OAXGuiFrame( void ) {
	vec3_t start, end, forward;
	trace_t tr;
	float xyf[3];
	int focus = 0;
	cgOaxGui_t *g;

	if ( !cgGuiOn ) {
		return;
	}
	CG_OAXGuiUpdate();

	if ( cg.snap && cg.predictedPlayerState.stats[STAT_HEALTH] > 0 &&
			cg.predictedPlayerState.pm_type == PM_NORMAL ) {
		/* the player's eye, as the server traces it (not the rendered view,
		   which a third-person or overridden camera moves) */
		VectorCopy( cg.predictedPlayerState.origin, start );
		start[2] += cg.predictedPlayerState.viewheight;
		AngleVectors( cg.predictedPlayerState.viewangles, forward, NULL, NULL );
		VectorMA( start, GUI_FOCUS_DIST, forward, end );
		CG_Trace( &tr, start, NULL, NULL, end, cg.predictedPlayerState.clientNum, MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum < ENTITYNUM_MAX_NORMAL ) {
			centity_t *cent = &cg_entities[tr.entityNum];
			g = CG_OAXGuiForEntity( tr.entityNum );
			if ( g && cent->currentState.solid == SOLID_BMODEL &&
					trap_OAX_CG_GuiTrace( cent->currentState.modelindex, cent->lerpOrigin, cent->lerpAngles,
						start, end, xyf ) ) {
				focus = 1 + ( g - cgGuis );
				trap_OAX_CG_GuiCursor( g->handle, xyf[0], xyf[1] );
			}
		}
	}

	if ( focus != cgGuiFocus ) {
		if ( cgGuiFocus && cgGuis[cgGuiFocus - 1].handle ) {
			trap_OAX_CG_GuiActivate( cgGuis[cgGuiFocus - 1].handle, 0 );
		}
		if ( focus ) {
			trap_OAX_CG_GuiActivate( cgGuis[focus - 1].handle, 1 );
		}
		cgGuiFocus = focus;
	}
	BG_OAXDebugSetInt( "cg_gui_focus", cgGuiFocus );
}

/*
=================
CG_OAXGuiFilterCmd

From the prediction loop: a focused player's attack is a click, not a shot
(the server agrees, so no muzzle flash is mispredicted).
=================
*/
void CG_OAXGuiFilterCmd( usercmd_t *cmd ) {
	if ( cgGuiOn && cgGuiFocus ) {
		cmd->buttons &= ~BUTTON_ATTACK;
	}
}
