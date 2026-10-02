/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
===========================================================================
g_oax_gui.c: in-world GUIs (func_oax_gui), the game side.

The engine runs each GUI (a port of DOOM-3's GUI system); this module
decides who is using it. Every player think, the player's eye is traced
64 units ahead (DOOM-3's focus distance). On a func_oax_gui whose GUI
surface the ray crosses, the player is focused: the weapon does not fire,
BUTTON_ATTACK is the mouse button, and the cursor follows the aim. The
commands the GUI returns are run here:

  set "gui::<key>" "<value>"   state: published in CS_OAX_GUISTATE + slot,
                               which every client's copy applies
  activate                     use the entity's targets (G_UseTargets)
  activate <targetname>        use the entities with that targetname
  trigger <targetname>         the same
  runScript <function>         G_OAXGuiRunScript (the map script VM)
  print <text>                 a server console line

func_oax_gui (a brush entity; its face shader has "surfaceparm gui"):
  "gui"        the GUI file, e.g. "guis/door_panel.gui"
  "gui_parm*"  initial state values (DOOM-3: the GUI reads "gui::gui_parm1")
  "gui::*"     initial state values by name ("gui::status" "CLOSED")
  "target"     what "activate" uses

Adapted from DOOM-3 neo/game/Player.cpp (idPlayer::UpdateFocus: the eye
trace, focus enter and exit, the cursor parked then moved) and
neo/game/Entity.cpp (idEntity::HandleGuiCommands: activate, runScript,
print). Changes: C89 for the QVM; a world trace plus the engine's GUI
surface trace replace the render-model trace; BUTTON_ATTACK is the mouse
button; targets are Q3 targetnames (G_UseTargets); state goes to clients
through a configstring.

Server authority: clicks, focus and state come only from the server's
GUI, so every client and every build sees the same state sequence. With a
stock engine (no "gui" in oax_features) the entity is a plain solid panel.
===========================================================================
*/
#include "g_local.h"

#define GUI_FOCUS_DIST		64
#define MAX_OAX_GUIS		64		/* CS_OAX_GUISTATE .. +63 */
#define GUI_CMD_SIZE		1024
#define GUI_LOG_SIZE		24

typedef struct {
	gentity_t	*ent;
	int			handle;
	char		file[MAX_QPATH];
	char		cs[MAX_INFO_STRING];
} oaxGui_t;

static oaxGui_t	oaxGuis[MAX_OAX_GUIS];
static int		oaxNumGuis;
static int		oaxGuiOn;					/* the engine has the feature */
static int		focusedGui[MAX_CLIENTS];	/* 1 + index, 0 = none */
static int		guiLogCount;

int trap_OAX_GuiLoad( const char *file );
void trap_OAX_GuiFree( int handle );
void trap_OAX_GuiSetState( int handle, const char *key, const char *value );
int trap_OAX_GuiGetState( int handle, const char *key, char *buf, int size );
int trap_OAX_GuiHandleEvent( int handle, float x, float y, int buttons, int time, char *cmds, int size );
int trap_OAX_GuiTrace( int entnum, const vec3_t start, const vec3_t end, float *xyFrac );
int trap_OAX_GuiActivate( int handle, int activate, int time, char *cmds, int size );
int trap_OAX_GuiNamedEvent( int handle, const char *name, int time, char *cmds, int size );

/*
=================
G_OAXGuiLog

The GUI event log, published as g_guilog_<n> for the tests (the same
sequence on every build is the server-authority check).
=================
*/
static void G_OAXGuiLog( const char *line ) {
	if ( guiLogCount < GUI_LOG_SIZE ) {
		BG_OAXDebugSet( va( "g_guilog_%i", guiLogCount ), line );
		guiLogCount++;
		BG_OAXDebugSetInt( "g_guilog_n", guiLogCount );
	}
}

static void G_OAXGuiPublish( int index ) {
	oaxGui_t *g = &oaxGuis[index];
	trap_SetConfigstring( CS_OAX_GUISTATE + index, g->cs );
}

/* a "set" from the GUI: the state every client applies */
static void G_OAXGuiSetState( int index, const char *key, const char *value ) {
	oaxGui_t *g = &oaxGuis[index];
	char name[64];

	if ( Q_strncmp( key, "gui::", 5 ) ) {
		return;		/* window variables stay inside the GUI */
	}
	Info_SetValueForKey( g->cs, key, value );
	G_OAXGuiPublish( index );
	Com_sprintf( name, sizeof( name ), "g_gui%i_%s", index, key + 5 );
	BG_OAXDebugSet( name, value );
}

static void G_OAXGuiUseByName( gentity_t *self, gentity_t *activator, const char *targetname ) {
	gentity_t *t = NULL;

	while ( ( t = G_Find( t, FOFS( targetname ), targetname ) ) != NULL ) {
		if ( t != self && t->use ) {
			t->use( t, self, activator );
		}
	}
}

/*
=================
G_OAXGuiCommands

Runs a GUI's command string ("cmd ; cmd ; ..."), DOOM-3's
idEntity::HandleGuiCommands with Q3 targets.
=================
*/
static void G_OAXGuiCommands( int index, gentity_t *activator, char *cmds ) {
	gentity_t *self = oaxGuis[index].ent;
	char *p = cmds;
	char *tok;
	char key[MAX_TOKEN_CHARS];
	char value[MAX_TOKEN_CHARS];

	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			return;
		}
		if ( !Q_stricmp( tok, ";" ) ) {
			continue;
		}
		if ( !Q_stricmp( tok, "set" ) ) {
			Q_strncpyz( key, COM_Parse( &p ), sizeof( key ) );
			Q_strncpyz( value, COM_Parse( &p ), sizeof( value ) );
			G_OAXGuiLog( va( "gui%i set %s %s", index, key, value ) );
			G_OAXGuiSetState( index, key, value );
			continue;
		}
		if ( !Q_stricmp( tok, "activate" ) || !Q_stricmp( tok, "trigger" ) ) {
			char *save = p;
			char *arg = COM_Parse( &p );
			if ( !arg[0] || !Q_stricmp( arg, ";" ) ) {
				p = save;
				G_OAXGuiLog( va( "gui%i activate %s", index, self->target ? self->target : "-" ) );
				G_UseTargets( self, activator );
			} else {
				G_OAXGuiLog( va( "gui%i activate %s", index, arg ) );
				G_OAXGuiUseByName( self, activator, arg );
			}
			continue;
		}
		if ( !Q_stricmp( tok, "runScript" ) ) {
			Q_strncpyz( value, COM_Parse( &p ), sizeof( value ) );
			G_OAXGuiLog( va( "gui%i runScript %s", index, value ) );
			G_OAXGuiRunScript( value, self );
			continue;
		}
		if ( !Q_stricmp( tok, "print" ) ) {
			value[0] = '\0';
			while ( 1 ) {
				char *save = p;
				tok = COM_Parse( &p );
				if ( !tok[0] || !Q_stricmp( tok, ";" ) ) {
					p = save;
					break;
				}
				Q_strcat( value, sizeof( value ), tok );
				Q_strcat( value, sizeof( value ), " " );
			}
			G_Printf( "gui %i: %s\n", index, value );
			continue;
		}
		G_Printf( "func_oax_gui: unknown gui command '%s'\n", tok );
	}
}

/*
QUAKED func_oax_gui (0 .5 .8) ?
A panel that shows an interactive GUI on its "surfaceparm gui" face.
"gui"        GUI file (guis/name.gui)
"gui_parm*"  initial GUI state values
"gui::*"     initial GUI state values by name
"target"     used by the GUI's "activate" command
*/
void SP_func_oax_gui( gentity_t *ent ) {
	char *file;
	oaxGui_t *g;
	int i;

	trap_SetBrushModel( ent, ent->model );
	ent->s.eType = ET_MOVER;
	ent->s.pos.trType = TR_STATIONARY;
	VectorCopy( ent->s.origin, ent->s.pos.trBase );
	VectorCopy( ent->s.origin, ent->r.currentOrigin );
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	VectorCopy( ent->s.angles, ent->r.currentAngles );
	ent->r.contents = CONTENTS_SOLID;
	trap_LinkEntity( ent );

	if ( !BG_OAXFeature( "gui" ) ) {
		return;
	}
	oaxGuiOn = 1;
	G_SpawnString( "gui", "", &file );
	if ( !file[0] ) {
		G_Printf( "func_oax_gui at %s without a \"gui\" key\n", vtos( ent->s.origin ) );
		return;
	}
	if ( oaxNumGuis == MAX_OAX_GUIS ) {
		G_Printf( "func_oax_gui: more than %i GUIs\n", MAX_OAX_GUIS );
		return;
	}

	g = &oaxGuis[oaxNumGuis];
	memset( g, 0, sizeof( *g ) );
	g->ent = ent;
	Q_strncpyz( g->file, file, sizeof( g->file ) );
	g->handle = trap_OAX_GuiLoad( g->file );
	if ( !g->handle ) {
		G_Printf( "func_oax_gui: could not load %s\n", g->file );
		return;
	}

	Info_SetValueForKey( g->cs, "e", va( "%i", ent->s.number ) );
	Info_SetValueForKey( g->cs, "f", g->file );
	/* gui_parm keys go to the GUI state as they are (DOOM-3 behavior);
	   gui::<name> keys set the state variable <name> */
	for ( i = 0; i < level.numSpawnVars; i++ ) {
		const char *k = level.spawnVars[i][0];
		const char *v = level.spawnVars[i][1];
		if ( !Q_strncmp( k, "gui_parm", 8 ) ) {
			trap_OAX_GuiSetState( g->handle, k, v );
			Info_SetValueForKey( g->cs, va( "gui::%s", k ), v );
		} else if ( !Q_strncmp( k, "gui::", 5 ) && k[5] ) {
			trap_OAX_GuiSetState( g->handle, k, v );
			Info_SetValueForKey( g->cs, k, v );
		}
	}
	oaxNumGuis++;
	G_OAXGuiPublish( oaxNumGuis - 1 );
}

void G_OAXGuiInit( void ) {
	int i;

	memset( focusedGui, 0, sizeof( focusedGui ) );
	guiLogCount = 0;
	BG_OAXDebugSetInt( "g_gui_count", oaxNumGuis );
	for ( i = 0; i < oaxNumGuis; i++ ) {
		/* the state the panels start with, for the tests */
		char buf[MAX_INFO_VALUE];
		if ( !trap_OAX_GuiGetState( oaxGuis[i].handle, "gui::state", buf, sizeof( buf ) ) ) {
			buf[0] = '\0';
		}
		BG_OAXDebugSet( va( "g_gui%i_state", i ), buf );
	}
}

void G_OAXGuiShutdown( void ) {
	int i;

	for ( i = 0; i < oaxNumGuis; i++ ) {
		if ( oaxGuis[i].handle ) {
			trap_OAX_GuiFree( oaxGuis[i].handle );
		}
	}
	memset( oaxGuis, 0, sizeof( oaxGuis ) );
	oaxNumGuis = 0;
	oaxGuiOn = 0;
}

static int G_OAXGuiForEntity( int entnum ) {
	int i;

	for ( i = 0; i < oaxNumGuis; i++ ) {
		if ( oaxGuis[i].ent && oaxGuis[i].ent->s.number == entnum && oaxGuis[i].handle ) {
			return i;
		}
	}
	return -1;
}

static void G_OAXGuiFocus( gentity_t *player, int index, int on ) {
	char cmds[GUI_CMD_SIZE];

	G_OAXGuiLog( va( "gui%i %s %i", index, on ? "focus" : "unfocus", player->s.number ) );
	cmds[0] = '\0';
	trap_OAX_GuiActivate( oaxGuis[index].handle, on, level.time, cmds, sizeof( cmds ) );
	G_OAXGuiCommands( index, player, cmds );
}

/*
=================
G_OAXGuiClientThink

Before the player's move: focus, cursor and clicks. A focused player's
BUTTON_ATTACK is the GUI's mouse button and never fires the weapon.
=================
*/
void G_OAXGuiClientThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	int clientNum = ent->s.number;
	int index = -1;
	int buttons;
	vec3_t angles, forward, start, end;
	trace_t tr;
	float xyf[3];
	char cmds[GUI_CMD_SIZE];
	int i;

	if ( !oaxGuiOn || clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	if ( client->ps.stats[STAT_HEALTH] > 0 && client->ps.pm_type == PM_NORMAL ) {
		/* the view this command aims with */
		for ( i = 0; i < 3; i++ ) {
			angles[i] = SHORT2ANGLE( ucmd->angles[i] + client->ps.delta_angles[i] );
		}
		AngleVectors( angles, forward, NULL, NULL );
		VectorCopy( client->ps.origin, start );
		start[2] += client->ps.viewheight;
		VectorMA( start, GUI_FOCUS_DIST, forward, end );

		trap_Trace( &tr, start, NULL, NULL, end, clientNum, MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum < ENTITYNUM_MAX_NORMAL ) {
			i = G_OAXGuiForEntity( tr.entityNum );
			if ( i >= 0 && trap_OAX_GuiTrace( tr.entityNum, start, end, xyf ) ) {
				index = i;
			}
		}
	}

	if ( focusedGui[clientNum] && focusedGui[clientNum] - 1 != index ) {
		G_OAXGuiFocus( ent, focusedGui[clientNum] - 1, 0 );
		focusedGui[clientNum] = 0;
	}
	if ( index < 0 ) {
		return;
	}
	if ( !focusedGui[clientNum] ) {
		focusedGui[clientNum] = index + 1;
		G_OAXGuiFocus( ent, index, 1 );
	}

	buttons = ( ucmd->buttons & BUTTON_ATTACK ) ? 1 : 0;
	cmds[0] = '\0';
	trap_OAX_GuiHandleEvent( oaxGuis[index].handle, xyf[0], xyf[1], buttons, level.time, cmds, sizeof( cmds ) );
	if ( cmds[0] ) {
		G_OAXGuiLog( va( "gui%i event %.4f %.4f %i", index, xyf[0], xyf[1], buttons ) );
		G_OAXGuiCommands( index, ent, cmds );
	}

	/* the click belongs to the GUI */
	ucmd->buttons &= ~BUTTON_ATTACK;
}

/*
=================
G_OAXGuiRunFrame

Publishes, for the tests, what each GUI's first target is doing (a mover's
state and height), so "the button opened the door" is observable, and the
first player's ammo, so "the click did not fire" is.
=================
*/
void G_OAXGuiRunFrame( void ) {
	int i;
	gentity_t *t;
	vec3_t pos;

	if ( !oaxGuiOn ) {
		return;
	}
	if ( g_entities[0].inuse && g_entities[0].client ) {
		playerState_t *ps = &g_entities[0].client->ps;
		BG_OAXDebugSetInt( "g_p0_ammo", ps->ammo[ps->weapon] );
	}

	for ( i = 0; i < oaxNumGuis && i < 4; i++ ) {
		if ( !oaxGuis[i].ent || !oaxGuis[i].ent->target ) {
			continue;
		}
		t = G_Find( NULL, FOFS( targetname ), oaxGuis[i].ent->target );
		if ( !t || t->s.eType != ET_MOVER ) {
			continue;
		}
		BG_EvaluateTrajectory( &t->s.pos, level.time, pos );
		BG_OAXDebugSetInt( va( "g_gui%i_target_state", i ), t->moverState );
		BG_OAXDebugSetFloat( va( "g_gui%i_target_z", i ), pos[2] );
	}
}
