/*
===========================================================================
bg_oax_zone.c: zone volumes, shared by game and cgame (see bg_oax_zone.h).
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_local.h"
#include "bg_oax_zone.h"

bgOAXZone_t bg_oaxZones[MAX_OAX_ZONES];
int         bg_oaxNumZones;

/* up to three whitespace-separated numbers; missing ones stay 0 */
static void BG_OAXZoneVec( const char *s, vec3_t out ) {
	char buf[96];
	char *p, *start;
	int  i;

	VectorClear( out );
	Q_strncpyz( buf, s, sizeof( buf ) );
	p = buf;
	for ( i = 0; i < 3; i++ ) {
		while ( *p == ' ' ) {
			p++;
		}
		if ( !*p ) {
			return;
		}
		start = p;
		while ( *p && *p != ' ' ) {
			p++;
		}
		if ( *p ) {
			*p++ = '\0';
		}
		out[i] = atof( start );
	}
}

static float BG_OAXZoneFloat( const char *info, const char *key, float def ) {
	const char *v = Info_ValueForKey( info, key );

	return v[0] ? atof( v ) : def;
}

/*
=================
BG_OAXZoneParse

Fill slot from its configstring ("" clears it). Both modules parse the same
text, so their floats are bit-identical.
=================
*/
void BG_OAXZoneParse( int slot, const char *info ) {
	bgOAXZone_t *z;
	const char  *v;
	int          i;

	if ( slot < 0 || slot >= MAX_OAX_ZONES ) {
		return;
	}
	z = &bg_oaxZones[slot];
	memset( z, 0, sizeof( *z ) );
	if ( info && info[0] ) {
		z->active = atoi( Info_ValueForKey( info, "on" ) );
		z->entityNum = atoi( Info_ValueForKey( info, "e" ) );
		z->model = atoi( Info_ValueForKey( info, "m" ) );
		BG_OAXZoneVec( Info_ValueForKey( info, "o" ), z->origin );
		BG_OAXZoneVec( Info_ValueForKey( info, "lo" ), z->absmin );
		BG_OAXZoneVec( Info_ValueForKey( info, "hi" ), z->absmax );
		z->priority = atoi( Info_ValueForKey( info, "p" ) );

		v = Info_ValueForKey( info, "g" );
		if ( v[0] ) {
			z->hasGravity = 1;
			z->gravity = atof( v );
		}
		v = Info_ValueForKey( info, "gs" );
		if ( v[0] ) {
			z->hasGravity = 2;
			z->gravityScale = atof( v );
		}

		BG_OAXZoneVec( Info_ValueForKey( info, "c" ), z->current );
		VectorCopy( z->current, z->currentDir );
		z->currentSpeed = VectorNormalize( z->currentDir );
		z->currentAccel = BG_OAXZoneFloat( info, "ca", 1.0f );

		z->frictionScale = BG_OAXZoneFloat( info, "f", 1.0f );

		v = Info_ValueForKey( info, "fc" );
		if ( v[0] ) {
			z->hasFog = 1;
			BG_OAXZoneVec( v, z->fogColor );
			z->fogDensity = BG_OAXZoneFloat( info, "fd", 0.0f );
			z->fogStart = BG_OAXZoneFloat( info, "fs", 0.0f );
			z->fogEnd = BG_OAXZoneFloat( info, "fe", 0.0f );
		}

		Q_strncpyz( z->reverb, Info_ValueForKey( info, "r" ), sizeof( z->reverb ) );
		z->reverbDecay = BG_OAXZoneFloat( info, "rd", 0.0f );
		z->reverbGain = BG_OAXZoneFloat( info, "rg", 0.0f );

		z->damage = atoi( Info_ValueForKey( info, "d" ) );
	}

	bg_oaxNumZones = 0;
	for ( i = 0; i < MAX_OAX_ZONES; i++ ) {
		if ( bg_oaxZones[i].model ) {
			bg_oaxNumZones = i + 1;
		}
	}
}

/*
=================
BG_OAXZoneAtPoint

The zone containing point, or -1. Where zones overlap the highest priority
wins, then the lowest entity number.
=================
*/
int BG_OAXZoneAtPoint( const vec3_t point, bgOAXZoneContact_t contact ) {
	int                best = -1;
	int                i;
	const bgOAXZone_t *z;

	for ( i = 0; i < bg_oaxNumZones; i++ ) {
		z = &bg_oaxZones[i];
		if ( !z->active || !z->model ) {
			continue;
		}
		if ( point[0] < z->absmin[0] || point[0] > z->absmax[0] ||
		     point[1] < z->absmin[1] || point[1] > z->absmax[1] ||
		     point[2] < z->absmin[2] || point[2] > z->absmax[2] ) {
			continue;
		}
		if ( best >= 0 ) {
			if ( z->priority < bg_oaxZones[best].priority ) {
				continue;
			}
			if ( z->priority == bg_oaxZones[best].priority && z->entityNum > bg_oaxZones[best].entityNum ) {
				continue;
			}
		}
		if ( !contact( z, point ) ) {
			continue;
		}
		best = i;
	}
	return best;
}

/* gravity inside zone, given the level's gravity */
int BG_OAXZoneGravity( int zone, int levelGravity ) {
	const bgOAXZone_t *z;

	if ( zone < 0 || zone >= MAX_OAX_ZONES ) {
		return levelGravity;
	}
	z = &bg_oaxZones[zone];
	if ( z->hasGravity == 1 ) {
		return (int)z->gravity;
	}
	if ( z->hasGravity == 2 ) {
		return (int)( levelGravity * z->gravityScale );
	}
	return levelGravity;
}

/*
=================
PM_OAXZoneBegin / PM_OAXZoneEnd

PmoveSingle brackets every step with these. The zone is taken at the
origin where the step starts; its gravity replaces ps->gravity for the step
and the level's value is put back afterwards, so the player state that is
sent and predicted never carries zone gravity.
=================
*/
void PM_OAXZoneBegin( void ) {
	int zone;

	pml.oaxZone = 0;
	if ( !pm->oaxZoneAt ) {
		return;
	}
	zone = pm->oaxZoneAt( pm->ps->origin );
	if ( zone < 0 ) {
		return;
	}
	pml.oaxZone = zone + 1;
	pml.oaxLevelGravity = pm->ps->gravity;
	pm->ps->gravity = BG_OAXZoneGravity( zone, pm->ps->gravity );
}

void PM_OAXZoneEnd( void ) {
	if ( pml.oaxZone ) {
		pm->ps->gravity = pml.oaxLevelGravity;
	}
}

/*
=================
PM_OAXZoneCurrent

The zone's current pulls the velocity toward "current" at current_accel,
the way PM_Accelerate pulls toward the wish velocity (the q2 form, whatever
dmflags say, so a current never touches the other axes).
=================
*/
void PM_OAXZoneCurrent( void ) {
	const bgOAXZone_t *z;
	float              addspeed, accelspeed, currentspeed;
	int                i;

	if ( !pml.oaxZone ) {
		return;
	}
	z = &bg_oaxZones[pml.oaxZone - 1];
	if ( z->currentSpeed <= 0 ) {
		return;
	}
	currentspeed = DotProduct( pm->ps->velocity, z->currentDir );
	addspeed = z->currentSpeed - currentspeed;
	if ( addspeed <= 0 ) {
		return;
	}
	accelspeed = z->currentAccel * pml.frametime * z->currentSpeed;
	if ( accelspeed > addspeed ) {
		accelspeed = addspeed;
	}
	for ( i = 0; i < 3; i++ ) {
		pm->ps->velocity[i] += accelspeed * z->currentDir[i];
	}
}

float PM_OAXZoneFriction( void ) {
	if ( !pml.oaxZone ) {
		return 1.0f;
	}
	return bg_oaxZones[pml.oaxZone - 1].frictionScale;
}
