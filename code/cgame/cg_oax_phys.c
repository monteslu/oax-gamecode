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
cg_oax_phys.c: cosmetic physics in the cgame (Box3D through the engine's
"physics" syscalls, bg_oax_phys.h).

One cosmetic world per level: the map's solid brushes and patches as
static collision. Gibs and brass (LE_FRAGMENT local entities) become Box3D
bodies, sized from their model bounds, so they tumble, roll, stack and get
blown around; explosions push everything in the world and throw debris
chunks off the wall they hit. Nothing here feeds back into the game.

cg_physics 0 turns all of it off (the stock fragment code runs, as it does
on an engine without "physics"). cg_physDebris 0 keeps the explosion push
but throws no chunks. cg_physWorkers sets the world's Box3D worker count.
Ragdolls live in cg_oax_skel.c and share this world.
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_oax_phys.h"
#include "cg_oax_phys.h"

#define PHYS_CAT_WORLD		1u
#define PHYS_CAT_FRAGMENT	2u
#define PHYS_CAT_RAGDOLL	4u

#define PHYS_MAX_EVENTS		128

vmCvar_t	cg_physics;
vmCvar_t	cg_physDebris;
vmCvar_t	cg_physWorkers;

cgPhys_t	cgPhys;

static void CG_PhysCvars( void ) {
	trap_Cvar_Update( &cg_physics );
	trap_Cvar_Update( &cg_physDebris );
	trap_Cvar_Update( &cg_physWorkers );
}

/*
==================
CG_PhysInit

At level load (CG_OAXInit): the cosmetic world, made once per map.
==================
*/
void CG_PhysInit( void ) {
	oaxPhysWorldDef_t def;

	memset( &cgPhys, 0, sizeof( cgPhys ) );
	trap_Cvar_Register( &cg_physics, "cg_physics", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_physDebris, "cg_physDebris", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cg_physWorkers, "cg_physWorkers", "1", CVAR_ARCHIVE );
	BG_OAXDebugSetInt( "cg_phys_world", 0 );
	BG_OAXDebugSetInt( "cg_phys_ragdolls", 0 );
	BG_OAXDebugSetInt( "cg_phys_fragments", 0 );
	BG_OAXDebugSet( "cg_skel_ragdoll", "none" );
	if ( !cg_physics.integer || !BG_OAXFeature( "physics" ) ) {
		return;
	}
	BG_PhysWorldDefInit( &def, 800.0f );
	def.workerCount = cg_physWorkers.integer;
	cgPhys.world = trap_Phys_WorldCreate( &def );
	if ( !cgPhys.world ) {
		return;
	}
	trap_Phys_WorldAddBSP( cgPhys.world, CONTENTS_SOLID, PHYS_BSP_PATCHES, NULL );
	cgPhys.lastTime = cg.time;
	CG_SkelInit();
	BG_OAXDebugSetInt( "cg_phys_world", cgPhys.world );
}

qboolean CG_PhysActive( void ) {
	return cgPhys.world && cg_physics.integer;
}

/*
==============================================================================
fragments: gibs, brass, debris
==============================================================================
*/

static localEntity_t *CG_PhysFragmentLE( int userData ) {
	if ( userData < 1 || userData > CG_OAX_MAX_LOCAL_ENTITIES ) {
		return NULL;
	}
	return &cg_localEntities[userData - 1];
}

/*
==================
CG_PhysLaunchFragment

Called by CG_LaunchGib and the brass ejectors once their local entity is
set up: the entity's trajectory becomes a Box3D body (a box the size of
the model, or le->radius for debris chunks). Leaves the stock trajectory
alone when cosmetic physics is off.
==================
*/
void CG_PhysLaunchFragment( localEntity_t *le ) {
	oaxPhysBodyDef_t bd;
	oaxPhysShapeDef_t sd;
	vec3_t mins, maxs, ang;
	int i, body;

	if ( !CG_PhysActive() ) {
		return;
	}
	BG_PhysBodyDefInit( &bd, PHYS_BODY_DYNAMIC );
	VectorCopy( le->refEntity.origin, bd.origin );
	if ( le->leFlags & LEF_TUMBLE ) {
		VectorCopy( le->angles.trBase, ang );
		BG_QuatFromAngles( ang, bd.quat );
	}
	VectorCopy( le->pos.trDelta, bd.velocity );
	/* a spin from a hash, not rand(): the cgame's random sequence stays the
	   stock one, so every other effect looks as it always did */
	for ( i = 0; i < 3; i++ ) {
		unsigned h = ( (unsigned)( le - cg_localEntities ) * 2654435761u ) ^ ( (unsigned)cg.time * 40503u ) ^ ( i * 2246822519u );
		h ^= h >> 15;
		h *= 2246822519u;
		h ^= h >> 13;
		bd.angularVelocity[i] = ( (float)( h & 0xffff ) / 32767.5f - 1.0f ) * 12.0f;
	}
	bd.linearDamping = 0.05f;
	bd.angularDamping = 0.4f;
	bd.userData = ( le - cg_localEntities ) + 1;
	body = trap_Phys_BodyCreate( cgPhys.world, &bd );
	if ( !body ) {
		return;
	}

	BG_PhysShapeDefInit( &sd, PHYS_SHAPE_BOX, 900.0f );
	if ( le->refEntity.hModel ) {
		trap_R_ModelBounds( le->refEntity.hModel, mins, maxs );
		for ( i = 0; i < 3; i++ ) {
			float h = ( maxs[i] - mins[i] ) * 0.5f;
			sd.params[i] = h < 0.6f ? 0.6f : h > 14.0f ? 14.0f : h;
			sd.offset[i] = ( maxs[i] + mins[i] ) * 0.5f;
		}
	} else {
		VectorCopy( le->angles.trDelta, sd.params );	/* debris half extents */
	}
	sd.friction = 0.7f;
	sd.restitution = le->bounceFactor * 0.5f;
	sd.rollingResistance = 0.1f;
	sd.categoryBits = PHYS_CAT_FRAGMENT;
	sd.flags = PHYS_SHAPE_HITEVENTS;
	if ( !trap_Phys_BodyAddShape( body, &sd, NULL, 0, NULL, 0 ) ) {
		trap_Phys_BodyDestroy( body );
		return;
	}
	le->physBody = body;
	cgPhys.fragments++;
}

void CG_PhysFreeLocalEntity( localEntity_t *le ) {
	if ( le->physBody ) {
		if ( cgPhys.world ) {
			trap_Phys_BodyDestroy( le->physBody );
		}
		le->physBody = 0;
		cgPhys.fragments--;
	}
}

static void CG_PhysDrawBox( const vec3_t origin, vec3_t axis[3], const vec3_t half, qhandle_t shader, float shade ) {
	static const int faces[6][4] = {
		{ 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 }
	};
	static const float st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	vec3_t corners[8];
	polyVert_t verts[4];
	int i, j;

	for ( i = 0; i < 8; i++ ) {
		float x = ( i & 1 ) ? half[0] : -half[0];
		float y = ( i & 2 ) ? half[1] : -half[1];
		float z = ( i & 4 ) ? half[2] : -half[2];
		for ( j = 0; j < 3; j++ ) {
			corners[i][j] = origin[j] + axis[0][j] * x + axis[1][j] * y + axis[2][j] * z;
		}
	}
	for ( i = 0; i < 6; i++ ) {
		byte c = (byte)( 255.0f * shade * ( i == 5 ? 1.0f : i < 2 ? 0.8f : 0.6f ) );
		for ( j = 0; j < 4; j++ ) {
			VectorCopy( corners[faces[i][j]], verts[j].xyz );
			verts[j].st[0] = st[j][0] * half[0] / 16.0f;
			verts[j].st[1] = st[j][1] * half[1] / 16.0f;
			verts[j].modulate[0] = verts[j].modulate[1] = verts[j].modulate[2] = c;
			verts[j].modulate[3] = 255;
		}
		trap_R_AddPolyToScene( shader, 4, verts );
	}
}

/*
==================
CG_PhysAddFragment

CG_AddFragment for a fragment that has a body: draw it where Box3D has it,
sinking into the floor in its last second like the stock gibs.
==================
*/
void CG_PhysAddFragment( localEntity_t *le ) {
	oaxPhysBodyState_t st;
	refEntity_t *re = &le->refEntity;
	int t;

	if ( !trap_Phys_BodyGetState( le->physBody, &st ) ) {
		CG_FreeLocalEntity( le );
		return;
	}
	VectorCopy( st.origin, re->origin );
	BG_QuatToAxis( st.quat, re->axis );
	if ( CG_PointContents( re->origin, 0 ) & CONTENTS_NODROP ) {
		CG_FreeLocalEntity( le );
		return;
	}
	t = le->endTime - cg.time;
	if ( t < 1000 ) {
		VectorCopy( re->origin, re->lightingOrigin );
		re->renderfx |= RF_LIGHTING_ORIGIN;
		re->origin[2] -= 16 * ( 1.0f - (float)t / 1000.0f );
	}
	if ( !re->hModel ) {
		CG_PhysDrawBox( re->origin, re->axis, le->angles.trDelta, cgPhys.debrisShader, le->color[0] );
		return;
	}
	trap_R_AddRefEntityToScene( re );
	if ( le->leBounceSoundType == LEBS_BLOOD && ( st.flags & PHYS_STATE_AWAKE )
		&& VectorLengthSquared( st.velocity ) > 100.0f * 100.0f ) {
		CG_BloodTrail( le );
	}
}

/*
==================
CG_PhysDebris

Chunks of wall thrown off where an explosion hit: small boxes drawn with a
stone texture, flying out along the surface normal.
==================
*/
static void CG_PhysDebris( const vec3_t origin, const vec3_t dir, int count ) {
	localEntity_t *le;
	vec3_t org;
	int i;

	if ( !cgPhys.debrisShader ) {
		cgPhys.debrisShader = trap_R_RegisterShader( "textures/base_floor/concrete" );
	}
	for ( i = 0; i < count; i++ ) {
		le = CG_AllocLocalEntity();
		le->leType = LE_FRAGMENT;
		le->startTime = cg.time;
		le->endTime = cg.time + 4000 + random() * 2000;
		VectorMA( origin, 6.0f, dir, org );
		org[0] += crandom() * 4.0f;
		org[1] += crandom() * 4.0f;
		org[2] += crandom() * 4.0f;
		VectorCopy( org, le->refEntity.origin );
		AxisCopy( axisDefault, le->refEntity.axis );
		le->refEntity.hModel = 0;
		le->pos.trType = TR_GRAVITY;
		VectorCopy( org, le->pos.trBase );
		le->pos.trTime = cg.time;
		VectorScale( dir, 150.0f + random() * 250.0f, le->pos.trDelta );
		le->pos.trDelta[0] += crandom() * 160.0f;
		le->pos.trDelta[1] += crandom() * 160.0f;
		le->pos.trDelta[2] += 60.0f + random() * 160.0f;
		/* half extents of the chunk ride in angles.trDelta (unused by physics fragments) */
		le->angles.trDelta[0] = 0.8f + random() * 2.2f;
		le->angles.trDelta[1] = 0.8f + random() * 2.2f;
		le->angles.trDelta[2] = 0.6f + random() * 1.6f;
		le->color[0] = 0.6f + random() * 0.4f;
		le->bounceFactor = 0.3f;
		le->leBounceSoundType = LEBS_NONE;
		le->leMarkType = LEMT_NONE;
		CG_PhysLaunchFragment( le );
		if ( !le->physBody ) {
			CG_FreeLocalEntity( le );
			return;
		}
	}
}

/*
==================
CG_PhysExplosion

From CG_MissileHitWall: explosive weapons push every cosmetic body in
range, and throw chunks off the wall they hit.
==================
*/
void CG_PhysExplosion( int weapon, const vec3_t origin, const vec3_t dir, qboolean wall ) {
	float radius, impulse;
	int chunks;

	if ( !CG_PhysActive() ) {
		return;
	}
	switch ( weapon ) {
	case WP_ROCKET_LAUNCHER:
		radius = 180.0f; impulse = 900.0f; chunks = 8;
		break;
	case WP_GRENADE_LAUNCHER:
		radius = 180.0f; impulse = 900.0f; chunks = 8;
		break;
	case WP_BFG:
		radius = 260.0f; impulse = 1400.0f; chunks = 12;
		break;
	case WP_PLASMAGUN:
		radius = 60.0f; impulse = 150.0f; chunks = 0;
		break;
	default:
		return;
	}
	trap_Phys_WorldExplode( cgPhys.world, origin, radius, radius * 0.5f, impulse, 0 );
	if ( cg_physDebris.integer && chunks && wall ) {
		CG_PhysDebris( origin, dir, chunks );
	}
}

/*
==================
CG_PhysEvents

Hit events: the stock bounce sounds and blood marks, once per fragment.
==================
*/
static void CG_PhysFragmentHit( localEntity_t *le, const vec3_t point, const vec3_t normal ) {
	trace_t tr;

	memset( &tr, 0, sizeof( tr ) );
	VectorCopy( point, tr.endpos );
	VectorCopy( normal, tr.plane.normal );
	CG_FragmentBounceMark( le, &tr );
	if ( le->leBounceSoundType != LEBS_NONE ) {
		CG_FragmentBounceSound( le, &tr );
	}
}

static void CG_PhysEvents( void ) {
	static oaxPhysContact_t ev[PHYS_MAX_EVENTS];
	int i, n;
	vec3_t neg;

	n = trap_Phys_WorldContactEvents( cgPhys.world, ev, PHYS_MAX_EVENTS );
	for ( i = 0; i < n; i++ ) {
		localEntity_t *le;
		if ( ev[i].type != PHYS_CONTACT_HIT || ev[i].speed < 60.0f ) {
			continue;
		}
		/* the normal points from A to B: toward a fragment that is B */
		if ( ( le = CG_PhysFragmentLE( ev[i].userB ) ) && le->physBody == ev[i].bodyB ) {
			CG_PhysFragmentHit( le, ev[i].point, ev[i].normal );
		}
		if ( ( le = CG_PhysFragmentLE( ev[i].userA ) ) && le->physBody == ev[i].bodyA ) {
			VectorNegate( ev[i].normal, neg );
			CG_PhysFragmentHit( le, ev[i].point, neg );
		}
	}
}

/*
==================
CG_PhysFrame

Every frame (CG_OAXFrame): step the cosmetic world by the frame time and
publish what it holds.
==================
*/
void CG_PhysFrame( void ) {
	oaxPhysStats_t st;
	int msec;

	CG_PhysCvars();
	if ( !cgPhys.world ) {
		return;
	}
	msec = cg.time - cgPhys.lastTime;
	cgPhys.lastTime = cg.time;
	if ( msec < 0 || msec > 250 ) {
		msec = 0;	/* a map restart or a long hitch: don't fast-forward */
	}
	trap_Phys_WorldStep( cgPhys.world, msec );
	CG_PhysEvents();
	CG_SkelFrame();
	trap_Phys_WorldStats( cgPhys.world, &st );
	BG_OAXDebugSetInt( "cg_phys_bodies", st.bodies );
	BG_OAXDebugSetInt( "cg_phys_awake", st.awakeBodies );
	BG_OAXDebugSetInt( "cg_phys_fragments", cgPhys.fragments );
	BG_OAXDebugSetInt( "cg_phys_ragdolls", cgPhys.ragdolls );
	BG_OAXDebugSetFloat( "cg_phys_step_ms", st.lastStepMs );
	/* the lowest fragment, so a test can see nothing fell through the floor */
	if ( cgPhys.fragments > 0 ) {
		float minz = 99999.0f;
		int i;
		for ( i = 0; i < CG_OAX_MAX_LOCAL_ENTITIES; i++ ) {
			oaxPhysBodyState_t bs;
			if ( cg_localEntities[i].physBody && trap_Phys_BodyGetState( cg_localEntities[i].physBody, &bs ) && bs.origin[2] < minz ) {
				minz = bs.origin[2];
			}
		}
		BG_OAXDebugSetFloat( "cg_phys_frag_minz", minz );
	}
}

/*
==================
CG_PhysTest_f

cg_physTest <gib|explode|debris> [count | x y z]: cosmetic effects in front of
the view (or at x y z), for tests and screenshots.
==================
*/
void CG_PhysTest_f( void ) {
	char what[32];
	vec3_t fwd, org, up;
	int i, count;

	trap_Argv( 1, what, sizeof( what ) );
	count = trap_Argc() > 2 ? atoi( CG_Argv( 2 ) ) : 1;
	AngleVectors( cg.refdefViewAngles, fwd, NULL, NULL );
	VectorMA( cg.refdef.vieworg, 96.0f, fwd, org );
	VectorSet( up, 0, 0, 1 );
	if ( trap_Argc() > 4 ) {
		count = 1;
		org[0] = atof( CG_Argv( 2 ) );
		org[1] = atof( CG_Argv( 3 ) );
		org[2] = atof( CG_Argv( 4 ) );
	}
	for ( i = 0; i < count; i++ ) {
		if ( !Q_stricmp( what, "gib" ) ) {
			CG_GibPlayer( org );
		} else if ( !Q_stricmp( what, "debris" ) ) {
			if ( CG_PhysActive() ) {
				CG_PhysDebris( org, up, 8 );
			}
		} else if ( !Q_stricmp( what, "explode" ) ) {
			CG_PhysExplosion( WP_ROCKET_LAUNCHER, org, up, qtrue );
		}
	}
}
