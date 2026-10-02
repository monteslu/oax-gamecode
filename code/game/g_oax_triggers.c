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

g_oax_triggers.c: DOOM-3 triggers and targets as oax entities.

Adapted from DOOM-3 neo/game/Trigger.cpp (idTrigger_Count, idTrigger_Timer,
idTrigger_EntityName, idTrigger::CallScript) and neo/game/Target.cpp
(idTarget_SetKeyVal, idTarget_SetShaderParm), hand-ported from C++ to C89
for the QVM. Changed: entities are Quake III gentity_t (D3's PostEventSec
becomes think/nextthink, ActivateTargets becomes G_UseTargets, spawnArgs
are read once at spawn), activators are matched by targetname (or a
player's name) where D3 used the entity name, and random numbers come from
a per-entity generator so every build makes the same choices.

New classnames:
  target_oax_script       "call" a script function when used
  trigger_oax_count       fire after "count" activations ("repeat", "delay")
  trigger_oax_timer       fire every "wait" +- "random" s while on ("start_on",
                          "delay", "onName", "offName"); use toggles
  trigger_oax_entityname  touch trigger for one named entity ("entityname",
                          "wait", "random", "delay", "random_delay",
                          "triggerFirst", "noTouch")
  target_oax_setkeyval    set "keyval*" = "key;value" on its targets
  target_oax_shaderparm   set "shaderParm0".."shaderParm11" (and "_color")
                          on its targets; "toggle" flips 0/1 parms

Every one of them also takes "call" (and "target"), like D3's triggers.

===========================================================================
*/
#include "g_local.h"
#include "g_oax_script.h"

void InitTrigger( gentity_t *self );

/* a deterministic random number in [-1, 1] (D3 gameLocal.random.CRandomFloat) */
static float OAX_CRandom( gentity_t *ent ) {
	unsigned int x = (unsigned int)( ent->count + 1 ) * 2654435761u;

	x ^= (unsigned int)( level.time + ( ent - g_entities ) * 7919 );
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	ent->count = (int)( x & 0x7fffffff );
	return ( ( x >> 8 ) * ( 1.0f / 16777216.0f ) ) * 2.0f - 1.0f;
}

/* D3 SEC2MS */
static int SecToMs( float s ) {
	return (int)( s * 1000.0f + ( s < 0 ? -0.5f : 0.5f ) );
}

/*
================
OAX_TriggerAction

D3 idTrigger ActivateTargets + CallScript. G_UseTargets runs the "call"
key too (G_OAXScriptUseCall).
================
*/
static void OAX_TriggerAction( gentity_t *ent, gentity_t *activator ) {
	if ( !activator ) {
		activator = ent;
	}
	G_UseTargets( ent, activator );
}

/*
===============================================================================

  target_oax_script

===============================================================================
*/

static void Use_target_oax_script( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	/* G_UseTargets fires the "call" and any targets */
	G_UseTargets( self, activator );
}

void SP_target_oax_script( gentity_t *ent ) {
	ent->use = Use_target_oax_script;
}

/*
===============================================================================

  trigger_oax_count (D3 idTrigger_Count)

  ent->count holds the activations so far, ent->health the goal (-1 when
  exhausted), ent->wait the delay, spawnflags 1 = repeat.

===============================================================================
*/

static void Think_trigger_oax_count( gentity_t *ent ) {
	/* D3 idTrigger_Count::Event_TriggerAction */
	OAX_TriggerAction( ent, ent->activator );
	if ( ent->health == -1 ) {
		ent->think = G_FreeEntity;
		ent->nextthink = level.time + FRAMETIME;
	}
}

static void Use_trigger_oax_count( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	/* D3 idTrigger_Count::Event_Trigger: goal of -1 means exhausted */
	if ( ent->health >= 0 ) {
		ent->count++;
		if ( ent->count >= ent->health ) {
			if ( ent->spawnflags & 1 ) {
				ent->count = 0;
			} else {
				ent->health = -1;
			}
			ent->activator = activator;
			if ( ent->wait > 0 ) {
				ent->think = Think_trigger_oax_count;
				ent->nextthink = level.time + SecToMs( ent->wait );
			} else {
				Think_trigger_oax_count( ent );
			}
		}
	}
	BG_OAXDebugSet( va( "g_count_%s", ent->targetname ? ent->targetname : "?" ), va( "%d %d", ent->count, ent->health ) );
}

void SP_trigger_oax_count( gentity_t *ent ) {
	int repeat;

	/* D3 idTrigger_Count::Spawn */
	G_SpawnInt( "count", "1", &ent->health );
	G_SpawnFloat( "delay", "0", &ent->wait );
	G_SpawnInt( "repeat", "0", &repeat );
	if ( repeat ) {
		ent->spawnflags |= 1;
	}
	ent->count = 0;
	ent->use = Use_trigger_oax_count;
	if ( ent->model && ent->model[0] == '*' ) {
		InitTrigger( ent );
		ent->r.contents = 0;	/* activated, not touched (as in D3) */
		trap_LinkEntity( ent );
	}
}

/*
===============================================================================

  trigger_oax_timer (D3 idTrigger_Timer)

  ent->wait, ent->random as D3; ent->speed holds the start delay;
  spawnflags 1 = on. onName/offName are kept as message/team strings.

===============================================================================
*/

#define TIMER_ON	1

static char *timerOnName[MAX_GENTITIES];
static char *timerOffName[MAX_GENTITIES];

static void Think_trigger_oax_timer( gentity_t *ent ) {
	/* D3 idTrigger_Timer::Event_Timer */
	OAX_TriggerAction( ent, ent );

	/* set time before next firing */
	if ( ent->wait >= 0.0f ) {
		ent->think = Think_trigger_oax_timer;
		ent->nextthink = level.time + SecToMs( ent->wait + OAX_CRandom( ent ) * ent->random );
		if ( ent->nextthink <= level.time ) {
			ent->nextthink = level.time + 1;
		}
	}
}

static void StartTimer( gentity_t *ent ) {
	ent->think = Think_trigger_oax_timer;
	ent->nextthink = level.time + SecToMs( ent->speed );
	if ( ent->nextthink <= level.time ) {
		ent->nextthink = level.time + 1;
	}
}

static const char *ActivatorName( gentity_t *activator ) {
	if ( !activator ) {
		return "";
	}
	if ( activator->client ) {
		return activator->client->pers.netname;
	}
	return activator->targetname ? activator->targetname : "";
}

static void Use_trigger_oax_timer( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	int n = ent - g_entities;

	/* D3 idTrigger_Timer::Event_Use */
	if ( ent->spawnflags & TIMER_ON ) {
		if ( timerOffName[n] && Q_stricmp( timerOffName[n], ActivatorName( activator ) ) ) {
			return;
		}
		ent->spawnflags &= ~TIMER_ON;
		ent->nextthink = 0;
	} else {
		if ( timerOnName[n] && Q_stricmp( timerOnName[n], ActivatorName( activator ) ) ) {
			return;
		}
		ent->spawnflags |= TIMER_ON;
		StartTimer( ent );
	}
}

void SP_trigger_oax_timer( gentity_t *ent ) {
	int		on;
	char	*s;
	int		n = ent - g_entities;

	/* D3 idTrigger_Timer::Spawn */
	G_SpawnFloat( "random", "1", &ent->random );
	G_SpawnFloat( "wait", "1", &ent->wait );
	G_SpawnInt( "start_on", "0", &on );
	G_SpawnFloat( "delay", "0", &ent->speed );
	G_SpawnString( "onName", "", &s );
	timerOnName[n] = s[0] ? G_NewString( s ) : NULL;
	G_SpawnString( "offName", "", &s );
	timerOffName[n] = s[0] ? G_NewString( s ) : NULL;

	if ( ent->random >= ent->wait && ent->wait >= 0 ) {
		ent->random = ent->wait - 0.001f;
		G_Printf( "trigger_oax_timer at %s has random >= wait\n", vtos( ent->s.origin ) );
	}

	ent->count = n;
	ent->use = Use_trigger_oax_timer;
	ent->spawnflags &= ~TIMER_ON;
	if ( on ) {
		ent->spawnflags |= TIMER_ON;
		StartTimer( ent );
	}
}

/*
===============================================================================

  trigger_oax_entityname (D3 idTrigger_EntityName)

  ent->wait, ent->random, ent->speed = delay, entityRandomDelay[ent - g_entities] = random_delay,
  ent->pain_debounce_time = next trigger time; spawnflags 1 = triggerFirst

===============================================================================
*/

static char *entityNames[MAX_GENTITIES];
static float entityRandomDelay[MAX_GENTITIES];

static void TriggerAction_entityname( gentity_t *ent, gentity_t *activator ) {
	/* D3 idTrigger_EntityName::TriggerAction */
	OAX_TriggerAction( ent, activator );
	if ( ent->wait >= 0 ) {
		ent->pain_debounce_time = level.time + SecToMs( ent->wait + ent->random * OAX_CRandom( ent ) );
	} else {
		/* we can't just remove (this) here, because this is a touch function
		   called while looping through area links... */
		ent->pain_debounce_time = level.time + 1;
		ent->touch = NULL;
		ent->use = NULL;
		ent->think = G_FreeEntity;
		ent->nextthink = level.time + FRAMETIME;
	}
}

static void Think_entityname( gentity_t *ent ) {
	TriggerAction_entityname( ent, ent->activator );
}

static void Fire_entityname( gentity_t *ent, gentity_t *activator ) {
	if ( ent->pain_debounce_time > level.time ) {
		/* can't retrigger until the wait is over */
		return;
	}
	if ( !activator || !entityNames[ent - g_entities] || Q_stricmp( ActivatorName( activator ), entityNames[ent - g_entities] ) ) {
		return;
	}
	/* don't allow it to trigger twice in a single frame */
	ent->pain_debounce_time = level.time + 1;
	if ( ent->speed > 0 ) {
		/* don't allow it to trigger again until our delay has passed */
		ent->pain_debounce_time += SecToMs( ent->speed + entityRandomDelay[ent - g_entities] * OAX_CRandom( ent ) );
		ent->activator = activator;
		ent->think = Think_entityname;
		ent->nextthink = level.time + SecToMs( ent->speed );
	} else {
		TriggerAction_entityname( ent, activator );
	}
}

static void Use_trigger_oax_entityname( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	/* D3 idTrigger_EntityName::Event_Trigger */
	if ( ent->spawnflags & 1 ) {
		ent->spawnflags &= ~1;
		return;
	}
	Fire_entityname( ent, activator );
}

static void Touch_trigger_oax_entityname( gentity_t *ent, gentity_t *other, trace_t *trace ) {
	/* D3 idTrigger_EntityName::Event_Touch */
	if ( ent->spawnflags & 1 ) {
		return;
	}
	Fire_entityname( ent, other );
}

void SP_trigger_oax_entityname( gentity_t *ent ) {
	char	*s;
	int		first, noTouch;

	/* D3 idTrigger_EntityName::Spawn */
	G_SpawnFloat( "wait", "0.5", &ent->wait );
	G_SpawnFloat( "random", "0", &ent->random );
	G_SpawnFloat( "delay", "0", &ent->speed );
	G_SpawnFloat( "random_delay", "0", &entityRandomDelay[ent - g_entities] );
	if ( ent->random && ( ent->random >= ent->wait ) && ( ent->wait >= 0 ) ) {
		ent->random = ent->wait - 1;
		G_Printf( "trigger_oax_entityname at %s has random >= wait\n", vtos( ent->s.origin ) );
	}
	if ( entityRandomDelay[ent - g_entities] && ( entityRandomDelay[ent - g_entities] >= ent->speed ) && ( ent->speed >= 0 ) ) {
		entityRandomDelay[ent - g_entities] = ent->speed - 1;
		G_Printf( "trigger_oax_entityname at %s has random_delay >= delay\n", vtos( ent->s.origin ) );
	}
	G_SpawnInt( "triggerFirst", "0", &first );
	if ( first ) {
		ent->spawnflags |= 1;
	} else {
		ent->spawnflags &= ~1;
	}
	G_SpawnString( "entityname", "", &s );
	if ( !s[0] ) {
		G_Printf( S_COLOR_YELLOW "trigger_oax_entityname at %s doesn't have 'entityname' key specified\n", vtos( ent->s.origin ) );
	}
	entityNames[ent - g_entities] = s[0] ? G_NewString( s ) : NULL;
	ent->pain_debounce_time = 0;
	ent->count = ent - g_entities;
	ent->use = Use_trigger_oax_entityname;

	G_SpawnInt( "noTouch", "0", &noTouch );
	InitTrigger( ent );
	if ( noTouch ) {
		ent->r.contents = 0;
	} else {
		ent->touch = Touch_trigger_oax_entityname;
	}
	trap_LinkEntity( ent );
}

/*
===============================================================================

  target_oax_setkeyval (D3 idTarget_SetKeyVal)

===============================================================================
*/

#define MAX_KEYVALS	8
static char *keyvals[MAX_GENTITIES][MAX_KEYVALS];

static void Use_target_oax_setkeyval( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	gentity_t	*t;
	int			i, n;
	char		key[MAX_QPATH];
	const char	*kv, *semi;

	if ( !self->target ) {
		return;
	}
	t = NULL;
	while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
		for ( i = 0; i < MAX_KEYVALS; i++ ) {
			kv = keyvals[self - g_entities][i];
			if ( !kv ) {
				continue;
			}
			semi = strchr( kv, ';' );
			n = semi ? semi - kv : 0;
			if ( n <= 0 || n >= (int)sizeof( key ) ) {
				continue;
			}
			Q_strncpyz( key, kv, n + 1 );
			G_OAXSetEntityKey( t, key, semi + 1 );
		}
	}
}

void SP_target_oax_setkeyval( gentity_t *ent ) {
	int		i, n = 0;
	char	*s;

	for ( i = 0; i < level.numSpawnVars && n < MAX_KEYVALS; i++ ) {
		if ( !Q_stricmpn( level.spawnVars[i][0], "keyval", 6 ) ) {
			s = level.spawnVars[i][1];
			keyvals[ent - g_entities][n++] = G_NewString( s );
		}
	}
	for ( ; n < MAX_KEYVALS; n++ ) {
		keyvals[ent - g_entities][n] = NULL;
	}
	ent->use = Use_target_oax_setkeyval;
}

/*
===============================================================================

  target_oax_shaderparm (D3 idTarget_SetShaderParm)

===============================================================================
*/

static float	shaderParmValues[MAX_GENTITIES][12];
static int		shaderParmSet[MAX_GENTITIES];		/* bit per parm */

static void Use_target_oax_shaderparm( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	gentity_t	*t;
	int			n = self - g_entities;
	int			parmnum;
	float		value;

	if ( !self->target ) {
		return;
	}
	for ( parmnum = 0; parmnum < 12; parmnum++ ) {
		if ( !( shaderParmSet[n] & ( 1 << parmnum ) ) ) {
			continue;
		}
		value = shaderParmValues[n][parmnum];
		t = NULL;
		while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
			G_OAXSetShaderParm( t, parmnum, value );
		}
		/* "toggle" flips 0/1 parms for the next activation */
		if ( ( self->spawnflags & 1 ) && ( value == 0 || value == 1 ) ) {
			shaderParmValues[n][parmnum] = value == 0 ? 1.0f : 0.0f;
		}
	}
}

void SP_target_oax_shaderparm( gentity_t *ent ) {
	int		n = ent - g_entities;
	int		parmnum, toggle;
	float	value;
	vec3_t	color;
	char	*s;

	shaderParmSet[n] = 0;
	for ( parmnum = 0; parmnum < 12; parmnum++ ) {
		if ( G_SpawnFloat( va( "shaderParm%d", parmnum ), "0", &value ) ) {
			shaderParmValues[n][parmnum] = value;
			shaderParmSet[n] |= 1 << parmnum;
		}
	}
	/* D3 "_color" sets parms 0-2 */
	if ( G_SpawnString( "_color", "", &s ) && sscanf( s, "%f %f %f", &color[0], &color[1], &color[2] ) == 3 ) {
		for ( parmnum = 0; parmnum < 3; parmnum++ ) {
			shaderParmValues[n][parmnum] = color[parmnum];
			shaderParmSet[n] |= 1 << parmnum;
		}
	}
	G_SpawnInt( "toggle", "0", &toggle );
	if ( toggle ) {
		ent->spawnflags |= 1;
	}
	ent->use = Use_target_oax_shaderparm;
}
