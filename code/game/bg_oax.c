/*
===========================================================================
bg_oax.c: oax engine-extension helpers shared by game and cgame.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "oax_public.h"

void trap_Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize );
void trap_OAX_DebugSet( const char *name, const char *value );

static char oaxFeatures[MAX_CVAR_VALUE_STRING];
static int  oaxFeaturesRead;

/*
=================
BG_OAXFeature

True when the engine advertises token in its read-only cvar oax_features.
A stock engine has no such cvar, so every extension stays off there.
=================
*/
int BG_OAXFeature( const char *token ) {
	char padded[MAX_CVAR_VALUE_STRING + 2];
	char want[64];

	if ( !oaxFeaturesRead ) {
		trap_Cvar_VariableStringBuffer( "oax_features", oaxFeatures, sizeof( oaxFeatures ) );
		oaxFeaturesRead = 1;
	}
	Com_sprintf( padded, sizeof( padded ), " %s ", oaxFeatures );
	Com_sprintf( want, sizeof( want ), " %s ", token );
	return strstr( padded, want ) != NULL;
}

void BG_OAXDebugSet( const char *name, const char *value ) {
	if ( BG_OAXFeature( "debug" ) ) {
		trap_OAX_DebugSet( name, value );
	}
}

void BG_OAXDebugSetInt( const char *name, int value ) {
	BG_OAXDebugSet( name, va( "%i", value ) );
}

void BG_OAXDebugSetFloat( const char *name, float value ) {
	BG_OAXDebugSet( name, va( "%f", value ) );
}
