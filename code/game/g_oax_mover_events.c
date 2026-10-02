/*
===========================================================================
g_oax_mover_events.c: the DOOM-3 mover script events (game/Mover.cpp
idMover::Event_*) on oax movers (g_oax_mover.c), and the thread callback
that wakes sys.waitFor().

New code for oax (GPLv3, as the id Tech 4 code it follows); the event
names, formats and semantics are D3's.
===========================================================================
*/
#include "g_local.h"
#include "g_oax_script.h"
#include "g_oax_mover.h"

/* D3 idThread::ObjectMoveDone */
static void MoverDone( gentity_t *ent, int threadNum ) {
	trap_OAX_ScriptObjectDone( threadNum, G_OAXScriptHandle( ent ) );
}

static void Ev_MoveTo( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *target = G_OAXArgEntity( call, 0 );

	if ( target ) {
		G_OAXMoverMoveTo( self, target );
	}
}

static void Ev_MoveToPos( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	vec3_t v;

	G_OAXArgVector( call, 0, v );
	G_OAXMoverMoveToPos( self, v );
}

static void Ev_RotateTo( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	vec3_t v;

	G_OAXArgVector( call, 0, v );
	G_OAXMoverRotateTo( self, v );
}

static void Ev_RotateOnce( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	vec3_t v;

	G_OAXArgVector( call, 0, v );
	G_OAXMoverRotateOnce( self, v );
}

static void Ev_Speed( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXMoverSetSpeed( self, G_OAXArgFloat( call, 0 ) );
}

static void Ev_Time( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXMoverSetTime( self, G_OAXArgFloat( call, 0 ) );
}

static void Ev_AccelTime( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXMoverSetAccelTime( self, G_OAXArgFloat( call, 0 ) );
}

static void Ev_DecelTime( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXMoverSetDecelTime( self, G_OAXArgFloat( call, 0 ) );
}

static void Ev_StartSpline( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	gentity_t *spline = G_OAXArgEntity( call, 0 );

	if ( spline ) {
		G_OAXMoverStartSpline( self, spline );
	}
}

static void Ev_StopSpline( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXMoverStopSpline( self );
}

static void Ev_IsMoving( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXRetFloat( ret, G_OAXMoverIsMoving( self ) ? 1.0f : 0.0f );
}

static void Ev_IsRotating( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	G_OAXRetFloat( ret, G_OAXMoverIsRotating( self ) ? 1.0f : 0.0f );
}

/* D3 idMover::Event_SetCallback, the entity side of sys.waitFor() */
static void Ev_SetCallback( gentity_t *self, const oaxScriptCall_t *call, oaxScriptValue_t *ret, char *rs ) {
	ret->type = OAX_SV_INT;
	ret->i = G_OAXMoverSetCallback( self, call->thread );
}

#define ENT	OAX_EVENT_ENTITY

void G_OAXRegisterMoverEvents( void ) {
	g_oaxMoverDone = MoverDone;
	G_OAXScriptRegisterEvent( "moveTo", "e", 0, ENT, Ev_MoveTo );
	G_OAXScriptRegisterEvent( "moveToPos", "v", 0, ENT, Ev_MoveToPos );
	G_OAXScriptRegisterEvent( "rotateTo", "v", 0, ENT, Ev_RotateTo );
	G_OAXScriptRegisterEvent( "rotateOnce", "v", 0, ENT, Ev_RotateOnce );
	G_OAXScriptRegisterEvent( "speed", "f", 0, ENT, Ev_Speed );
	G_OAXScriptRegisterEvent( "time", "f", 0, ENT, Ev_Time );
	G_OAXScriptRegisterEvent( "accelTime", "f", 0, ENT, Ev_AccelTime );
	G_OAXScriptRegisterEvent( "decelTime", "f", 0, ENT, Ev_DecelTime );
	G_OAXScriptRegisterEvent( "startSpline", "e", 0, ENT, Ev_StartSpline );
	G_OAXScriptRegisterEvent( "stopSpline", "", 0, ENT, Ev_StopSpline );
	G_OAXScriptRegisterEvent( "isMoving", "", 'f', ENT, Ev_IsMoving );
	G_OAXScriptRegisterEvent( "isRotating", "", 'f', ENT, Ev_IsRotating );
	/* not declared in scripts: sys.waitFor asks it (D3 EV_Thread_SetCallback) */
	G_OAXScriptRegisterEvent( "<script_setcallback>", "", 'd', ENT, Ev_SetCallback );
}
