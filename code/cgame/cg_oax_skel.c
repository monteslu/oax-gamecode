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
cg_oax_skel.c: skeletal (IQM) players and their ragdolls.

A player model directory with a skeleton.cfg (see
models/players/iqmguy/skeleton.cfg) is one IQM for the whole body instead
of Q3's lower/upper/head MD3s. The cgame poses its skeleton every frame
(procedural legs and arms from the Q3 animation state and the view
angles, the IQM's own animations for gestures) and draws it with the
engine's skeletal entity call (token "physics_skel").

On death the body becomes a ragdoll in the cosmetic physics world
(cg_oax_phys.c): one capsule per configured bone, ball joints with cone
and twist limits, started from the last living pose and the player's
velocity. Every joint follows the bone body above it, so the whole mesh
is skinned from the ragdoll. The ragdoll stays with the corpse: the dead
player first, then the body-queue copy the server makes at respawn, and
sinks with it; a gib removes it. Without physics the corpse is drawn
lying down.

MD3 players never come here and keep the stock death animations.

Matrices are 3x4 row major (m[r * 4 + c], column 3 the translation), the
IQM and engine convention (oax_phys.h PHYS_SKEL_MAT_FLOATS).
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_oax_phys.h"
#include "cg_oax_phys.h"

int		trap_Phys_R_ModelSkeleton( qhandle_t model, oaxSkelJoint_t *out, int max );
int		trap_Phys_R_LerpSkeleton( qhandle_t model, int frame, int oldframe, float backlerp, float *mats, int max );
void	trap_Phys_R_AddSkeletalEntity( const refEntity_t *re, const float *mats, int numJoints );
int		trap_Phys_R_ModelFrames( qhandle_t model );

#define SKEL_MAX_DEFS		8
#define SKEL_MAX_JOINTS		64
#define SKEL_MAX_BONES		16
#define SKEL_MAX_RAGDOLLS	16
#define SKEL_RAGDOLL_IDLE	1500	/* ms a ragdoll survives without being drawn */
#define SKEL_RAGDOLL_LIFE	60000

typedef enum {
	ROLE_PELVIS, ROLE_SPINE, ROLE_HEAD, ROLE_ARM_L, ROLE_ARM_R, ROLE_LEG_L, ROLE_LEG_R, ROLE_HAND_R,
	ROLE_COUNT
} skelRole_t;

static const char *skelRoleNames[ROLE_COUNT] = {
	"pelvis", "spine", "head", "arm_l", "arm_r", "leg_l", "leg_r", "hand_r"
};

typedef struct {
	int		joint;
	int		tailJoint;		/* -1: tailLen along the joint's +y */
	float	tailLen;
	float	radius;
	int		parent;			/* bone index, -1 */
	float	cone, lo, hi;	/* radians */
} skelBone_t;

typedef struct {
	char			name[MAX_QPATH];
	qhandle_t		model, skin;
	int				numJoints, numFrames;
	oaxSkelJoint_t	joints[SKEL_MAX_JOINTS];
	float			bind[SKEL_MAX_JOINTS][12];		/* model space */
	float			local[SKEL_MAX_JOINTS][12];		/* bind relative to the parent */
	float			scale, yaw;
	vec3_t			offset, swingAxis, forwardAxis;
	float			armDrop;
	int				role[ROLE_COUNT];
	int				danceFirst, danceNum, jumpFirst, jumpNum;
	float			danceFps, jumpFps;
	int				numBones;
	skelBone_t		bones[SKEL_MAX_BONES];
	int				jointBone[SKEL_MAX_JOINTS];
} skelDef_t;

typedef struct {
	qboolean	active;
	int			def;
	int			clientNum;
	int			ownerEnt;
	int			startTime, lastDrawn;
	int			bodies[SKEL_MAX_BONES];
	float		jointLocal[SKEL_MAX_JOINTS][12];
	qboolean	sinkSet;
	float		sinkBase;
} skelRagdoll_t;

static skelDef_t		skelDefs[SKEL_MAX_DEFS];
static int				numSkelDefs;
static skelRagdoll_t	ragdolls[SKEL_MAX_RAGDOLLS];

/* the last living pose of each client (world space), the ragdoll's start */
static float			lastPose[MAX_CLIENTS][SKEL_MAX_JOINTS][12];
static int				lastPoseTime[MAX_CLIENTS];
static int				lastPoseDef[MAX_CLIENTS];
static vec3_t			lastPoseOrigin[MAX_CLIENTS];

/*
==============================================================================
3x4 matrix helpers
==============================================================================
*/

static void M34_Mul( const float *a, const float *b, float *out ) {
	float r[12];
	int i;

	for ( i = 0; i < 3; i++ ) {
		r[i * 4 + 0] = a[i * 4 + 0] * b[0] + a[i * 4 + 1] * b[4] + a[i * 4 + 2] * b[8];
		r[i * 4 + 1] = a[i * 4 + 0] * b[1] + a[i * 4 + 1] * b[5] + a[i * 4 + 2] * b[9];
		r[i * 4 + 2] = a[i * 4 + 0] * b[2] + a[i * 4 + 1] * b[6] + a[i * 4 + 2] * b[10];
		r[i * 4 + 3] = a[i * 4 + 0] * b[3] + a[i * 4 + 1] * b[7] + a[i * 4 + 2] * b[11] + a[i * 4 + 3];
	}
	memcpy( out, r, sizeof( r ) );
}

static void M34_Identity( float *m ) {
	memset( m, 0, 12 * sizeof( float ) );
	m[0] = m[5] = m[10] = 1.0f;
}

static void M34_FromQuat( const float *q, const float *t, float *m ) {
	vec3_t axis[3];
	int r;

	/* BG_QuatToAxis rows are the rotated basis vectors: columns of m */
	BG_QuatToAxis( q, axis );
	for ( r = 0; r < 3; r++ ) {
		m[r * 4 + 0] = axis[0][r];
		m[r * 4 + 1] = axis[1][r];
		m[r * 4 + 2] = axis[2][r];
		m[r * 4 + 3] = t ? t[r] : 0.0f;
	}
}

/* inverse of a rotation (no scale) + translation */
static void M34_InvertRigid( const float *m, float *out ) {
	float r[12];
	int i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			r[i * 4 + j] = m[j * 4 + i];
		}
	}
	for ( i = 0; i < 3; i++ ) {
		r[i * 4 + 3] = -( r[i * 4 + 0] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11] );
	}
	memcpy( out, r, sizeof( r ) );
}

/* rotation about a unit axis through the origin */
static void M34_AxisAngle( const vec3_t a, float rad, float *m ) {
	float c = cos( rad ), s = sin( rad ), t = 1.0f - c;

	m[0] = t * a[0] * a[0] + c;        m[1] = t * a[0] * a[1] - s * a[2]; m[2] = t * a[0] * a[2] + s * a[1];  m[3] = 0;
	m[4] = t * a[0] * a[1] + s * a[2]; m[5] = t * a[1] * a[1] + c;        m[6] = t * a[1] * a[2] - s * a[0];  m[7] = 0;
	m[8] = t * a[0] * a[2] - s * a[1]; m[9] = t * a[1] * a[2] + s * a[0]; m[10] = t * a[2] * a[2] + c;       m[11] = 0;
}

static void M34_Origin( const float *m, vec3_t out ) {
	out[0] = m[3];
	out[1] = m[7];
	out[2] = m[11];
}

static void M34_Column( const float *m, int c, vec3_t out ) {
	out[0] = m[c];
	out[1] = m[4 + c];
	out[2] = m[8 + c];
}

/*
==============================================================================
skeleton.cfg
==============================================================================
*/

static int CG_SkelJoint( const skelDef_t *d, const char *name ) {
	int i;

	for ( i = 0; i < d->numJoints; i++ ) {
		if ( !Q_stricmp( d->joints[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

static int CG_SkelBoneOfJoint( const skelDef_t *d, int joint ) {
	int i;

	for ( i = 0; i < d->numBones; i++ ) {
		if ( d->bones[i].joint == joint ) {
			return i;
		}
	}
	return -1;
}

static qboolean CG_SkelParse( skelDef_t *d, char *text ) {
	char *p = text, *tok;
	float drop = 80.0f;

	d->scale = 1.0f;
	VectorSet( d->swingAxis, 1, 0, 0 );
	VectorSet( d->forwardAxis, 0, 1, 0 );
	while ( 1 ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] ) {
			break;
		}
		if ( !Q_stricmp( tok, "model" ) ) {
			d->model = trap_R_RegisterModel( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "skin" ) ) {
			d->skin = trap_R_RegisterSkin( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "scale" ) ) {
			d->scale = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "yaw" ) ) {
			d->yaw = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "origin" ) || !Q_stricmp( tok, "swingaxis" ) || !Q_stricmp( tok, "forwardaxis" ) ) {
			float *v = !Q_stricmp( tok, "origin" ) ? d->offset : !Q_stricmp( tok, "swingaxis" ) ? d->swingAxis : d->forwardAxis;
			v[0] = atof( COM_ParseExt( &p, qfalse ) );
			v[1] = atof( COM_ParseExt( &p, qfalse ) );
			v[2] = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "armdrop" ) ) {
			drop = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "role" ) || !Q_stricmp( tok, "anim" ) || !Q_stricmp( tok, "bone" ) ) {
			/* need the skeleton: handled after the model loaded */
			if ( !d->model ) {
				return qfalse;
			}
			if ( !d->numJoints ) {
				d->numJoints = trap_Phys_R_ModelSkeleton( d->model, d->joints, SKEL_MAX_JOINTS );
				if ( d->numJoints <= 0 || d->numJoints > SKEL_MAX_JOINTS ) {
					return qfalse;
				}
			}
			if ( !Q_stricmp( tok, "role" ) ) {
				char role[64];
				int i;
				Q_strncpyz( role, COM_ParseExt( &p, qfalse ), sizeof( role ) );
				for ( i = 0; i < ROLE_COUNT; i++ ) {
					if ( !Q_stricmp( role, skelRoleNames[i] ) ) {
						d->role[i] = CG_SkelJoint( d, COM_ParseExt( &p, qfalse ) );
					}
				}
			} else if ( !Q_stricmp( tok, "anim" ) ) {
				char name[64];
				int first, num;
				float fps;
				Q_strncpyz( name, COM_ParseExt( &p, qfalse ), sizeof( name ) );
				first = atoi( COM_ParseExt( &p, qfalse ) );
				num = atoi( COM_ParseExt( &p, qfalse ) );
				fps = atof( COM_ParseExt( &p, qfalse ) );
				if ( !Q_stricmp( name, "dance" ) ) {
					d->danceFirst = first; d->danceNum = num; d->danceFps = fps;
				} else if ( !Q_stricmp( name, "jump" ) ) {
					d->jumpFirst = first; d->jumpNum = num; d->jumpFps = fps;
				}
			} else if ( d->numBones < SKEL_MAX_BONES ) {
				skelBone_t *b = &d->bones[d->numBones];
				char tail[64], parent[64];
				b->joint = CG_SkelJoint( d, COM_ParseExt( &p, qfalse ) );
				Q_strncpyz( tail, COM_ParseExt( &p, qfalse ), sizeof( tail ) );
				b->radius = atof( COM_ParseExt( &p, qfalse ) );
				Q_strncpyz( parent, COM_ParseExt( &p, qfalse ), sizeof( parent ) );
				b->cone = DEG2RAD( atof( COM_ParseExt( &p, qfalse ) ) );
				b->lo = DEG2RAD( atof( COM_ParseExt( &p, qfalse ) ) );
				b->hi = DEG2RAD( atof( COM_ParseExt( &p, qfalse ) ) );
				if ( !Q_stricmpn( tail, "len:", 4 ) ) {
					b->tailJoint = -1;
					b->tailLen = atof( tail + 4 );
				} else {
					b->tailJoint = CG_SkelJoint( d, tail );
				}
				b->parent = parent[0] == '-' ? -1 : CG_SkelBoneOfJoint( d, CG_SkelJoint( d, parent ) );
				if ( b->joint >= 0 && ( b->tailJoint >= 0 || b->tailLen > 0.0f ) && ( d->numBones == 0 || b->parent >= 0 ) ) {
					d->numBones++;
				}
			}
		}
		/* the rest of the line is a comment or garbage */
		SkipRestOfLine( &p );
	}
	d->armDrop = DEG2RAD( drop );
	return d->model && d->numJoints > 0;
}

static void CG_SkelPrepare( skelDef_t *d ) {
	float inv[12];
	int i, j;

	d->numFrames = trap_Phys_R_ModelFrames( d->model );
	VectorNormalize( d->swingAxis );
	VectorNormalize( d->forwardAxis );
	for ( i = 0; i < d->numJoints; i++ ) {
		M34_FromQuat( d->joints[i].quat, d->joints[i].origin, d->bind[i] );
		if ( d->joints[i].parent >= 0 ) {
			M34_InvertRigid( d->bind[d->joints[i].parent], inv );
			M34_Mul( inv, d->bind[i], d->local[i] );
		} else {
			memcpy( d->local[i], d->bind[i], sizeof( d->local[i] ) );
		}
	}
	/* each joint follows its own bone, the nearest bone above it, or the first */
	for ( i = 0; i < d->numJoints; i++ ) {
		d->jointBone[i] = 0;
		for ( j = i; j >= 0; j = d->joints[j].parent ) {
			int b = CG_SkelBoneOfJoint( d, j );
			if ( b >= 0 ) {
				d->jointBone[i] = b;
				break;
			}
		}
	}
	for ( i = 0; i < ROLE_COUNT; i++ ) {
		if ( d->role[i] >= d->numJoints ) {
			d->role[i] = -1;
		}
	}
}

/*
==================
CG_SkelRegisterClient

From CG_RegisterClientModelname: a model directory with a skeleton.cfg is a
skeletal player. Returns qtrue if it took the model (ci->oaxSkel set, every
model handle the stock code may touch pointing at the one IQM).
==================
*/
qboolean CG_SkelRegisterClient( clientInfo_t *ci, const char *modelName ) {
	char path[MAX_QPATH], text[4096];
	fileHandle_t f;
	skelDef_t *d;
	int i, len;

	ci->oaxSkel = 0;
	if ( !BG_OAXFeature( "physics_skel" ) ) {
		return qfalse;
	}
	for ( i = 0; i < numSkelDefs; i++ ) {
		if ( !Q_stricmp( skelDefs[i].name, modelName ) ) {
			break;
		}
	}
	if ( i == numSkelDefs ) {
		Com_sprintf( path, sizeof( path ), "models/players/%s/skeleton.cfg", modelName );
		len = trap_FS_FOpenFile( path, &f, FS_READ );
		if ( len <= 0 ) {
			return qfalse;
		}
		if ( len >= (int)sizeof( text ) || numSkelDefs == SKEL_MAX_DEFS ) {
			trap_FS_FCloseFile( f );
			return qfalse;
		}
		trap_FS_Read( text, len, f );
		text[len] = 0;
		trap_FS_FCloseFile( f );
		d = &skelDefs[numSkelDefs];
		memset( d, 0, sizeof( *d ) );
		for ( i = 0; i < ROLE_COUNT; i++ ) {
			d->role[i] = -1;
		}
		Q_strncpyz( d->name, modelName, sizeof( d->name ) );
		if ( !CG_SkelParse( d, text ) ) {
			Com_Printf( S_COLOR_YELLOW "%s: not a usable skeletal model\n", path );
			return qfalse;
		}
		CG_SkelPrepare( d );
		i = numSkelDefs++;
	}
	d = &skelDefs[i];
	ci->oaxSkel = i + 1;
	ci->legsModel = ci->torsoModel = ci->headModel = d->model;
	ci->legsSkin = ci->torsoSkin = ci->headSkin = d->skin;
	return qtrue;
}

/*
==============================================================================
posing
==============================================================================
*/

/* a model-space rotation of joint j turned into its local frame (conjugated
   by the joint's bind rotation), applied after its local bind transform */
static void CG_SkelRotate( const skelDef_t *d, int j, const vec3_t axis, float rad, float *localDelta ) {
	float rot[12], b[12], bi[12], tmp[12];

	if ( j < 0 || rad == 0.0f ) {
		return;
	}
	M34_AxisAngle( axis, rad, rot );
	memcpy( b, d->bind[j], sizeof( b ) );
	b[3] = b[7] = b[11] = 0.0f;
	M34_InvertRigid( b, bi );
	M34_Mul( bi, rot, tmp );
	M34_Mul( tmp, b, tmp );
	M34_Mul( localDelta, tmp, localDelta );
}

/* the sign that swings a limb hanging along the bone axis down when it
   turns about `axis`: the arm's drop direction */
static float CG_SkelDropSign( const skelDef_t *d, int j, const vec3_t axis ) {
	vec3_t dir, c;

	if ( j < 0 ) {
		return 0.0f;
	}
	M34_Column( d->bind[j], 1, dir );		/* bone axis = +y */
	CrossProduct( axis, dir, c );
	return c[2] > 0.0f ? -1.0f : 1.0f;
}

/*
==================
CG_SkelPose

The living pose in model space, from the animation state.
==================
*/
static void CG_SkelPose( const skelDef_t *d, centity_t *cent, const vec3_t legsAngles, const vec3_t torsoAngles,
		const vec3_t headAngles, float *mats ) {
	float delta[SKEL_MAX_JOINTS][12];
	int legsAnim = cent->pe.legs.animationNumber & ~ANIM_TOGGLEBIT;
	int torsoAnim = cent->pe.torso.animationNumber & ~ANIM_TOGGLEBIT;
	float speed, phase, swing = 0.0f, legAmp = 0.0f, armAmp = 0.0f, lean, crouch = 0.0f;
	vec3_t up;
	int i, dir = 1;

	/* a gesture plays the model's own dance */
	if ( torsoAnim == TORSO_GESTURE && d->danceNum > 0 && d->numFrames > d->danceFirst ) {
		float t = ( cg.time - cent->pe.torso.animationTime ) * 0.001f * d->danceFps;
		int f = (int)t;
		trap_Phys_R_LerpSkeleton( d->model, d->danceFirst + ( f % d->danceNum ), d->danceFirst + ( ( f + 1 ) % d->danceNum ),
			1.0f - ( t - f ), mats, SKEL_MAX_JOINTS );
		return;
	}

	VectorSet( up, 0, 0, 1 );
	for ( i = 0; i < d->numJoints; i++ ) {
		M34_Identity( delta[i] );
	}
	speed = sqrt( cent->currentState.pos.trDelta[0] * cent->currentState.pos.trDelta[0]
		+ cent->currentState.pos.trDelta[1] * cent->currentState.pos.trDelta[1] );
	phase = cg.time * 0.001f * ( 4.0f + speed * 0.012f );
	switch ( legsAnim ) {
	case LEGS_RUN:
		legAmp = 0.7f; armAmp = 0.6f;
		break;
	case LEGS_WALK:
		legAmp = 0.4f; armAmp = 0.3f;
		break;
	case LEGS_BACK:
		legAmp = 0.5f; armAmp = 0.3f; dir = -1;
		break;
	case LEGS_WALKCR:
		legAmp = 0.25f; crouch = 1.0f;
		break;
	case LEGS_IDLECR:
		crouch = 1.0f;
		break;
	case LEGS_SWIM:
		legAmp = 0.3f; armAmp = 0.8f; phase *= 0.6f;
		break;
	case LEGS_JUMP:
	case LEGS_JUMPB:
		swing = 0.5f;
		break;
	default:
		break;
	}
	if ( legAmp > 0.0f ) {
		swing = legAmp * sin( phase ) * dir;
	}
	/* legs: crouching sits the thighs forward */
	CG_SkelRotate( d, d->role[ROLE_LEG_L], d->swingAxis, swing + crouch * 1.1f, delta[d->role[ROLE_LEG_L] < 0 ? 0 : d->role[ROLE_LEG_L]] );
	CG_SkelRotate( d, d->role[ROLE_LEG_R], d->swingAxis,
		( legsAnim == LEGS_JUMP || legsAnim == LEGS_JUMPB ) ? -0.3f : -swing + crouch * 1.1f,
		delta[d->role[ROLE_LEG_R] < 0 ? 0 : d->role[ROLE_LEG_R]] );
	/* arms: down from the bind pose, swinging against the legs; the right one aims */
	if ( d->role[ROLE_ARM_L] >= 0 ) {
		CG_SkelRotate( d, d->role[ROLE_ARM_L], d->forwardAxis, d->armDrop * CG_SkelDropSign( d, d->role[ROLE_ARM_L], d->forwardAxis ),
			delta[d->role[ROLE_ARM_L]] );
		CG_SkelRotate( d, d->role[ROLE_ARM_L], d->swingAxis, -armAmp * sin( phase ) * dir, delta[d->role[ROLE_ARM_L]] );
	}
	if ( d->role[ROLE_ARM_R] >= 0 ) {
		CG_SkelRotate( d, d->role[ROLE_ARM_R], d->forwardAxis, d->armDrop * CG_SkelDropSign( d, d->role[ROLE_ARM_R], d->forwardAxis ),
			delta[d->role[ROLE_ARM_R]] );
		CG_SkelRotate( d, d->role[ROLE_ARM_R], d->swingAxis, torsoAnim == TORSO_ATTACK || torsoAnim == TORSO_ATTACK2 ? 1.45f : 1.25f,
			delta[d->role[ROLE_ARM_R]] );
	}
	/* the spine leans with the view pitch and turns toward where the torso aims */
	lean = DEG2RAD( AngleNormalize180( torsoAngles[PITCH] ) ) * 0.5f;
	if ( d->role[ROLE_SPINE] >= 0 ) {
		CG_SkelRotate( d, d->role[ROLE_SPINE], up, -DEG2RAD( AngleNormalize180( torsoAngles[YAW] - legsAngles[YAW] ) ),
			delta[d->role[ROLE_SPINE]] );
		CG_SkelRotate( d, d->role[ROLE_SPINE], d->swingAxis, -lean + crouch * 0.35f, delta[d->role[ROLE_SPINE]] );
	}
	if ( d->role[ROLE_HEAD] >= 0 ) {
		CG_SkelRotate( d, d->role[ROLE_HEAD], d->swingAxis, -DEG2RAD( AngleNormalize180( headAngles[PITCH] ) ) * 0.5f,
			delta[d->role[ROLE_HEAD]] );
	}

	for ( i = 0; i < d->numJoints; i++ ) {
		float m[12];
		M34_Mul( d->local[i], delta[i], m );
		if ( d->joints[i].parent >= 0 ) {
			M34_Mul( mats + d->joints[i].parent * 12, m, mats + i * 12 );
		} else {
			memcpy( mats + i * 12, m, sizeof( m ) );
		}
	}
}

/* model space -> world: the legs' axis, the model's own yaw and scale, origin + offset */
static void CG_SkelWorldMatrix( const skelDef_t *d, const vec3_t origin, vec3_t axis[3], float *e ) {
	float yaw[12], ax[12];
	vec3_t z;
	int r;

	VectorSet( z, 0, 0, 1 );
	M34_AxisAngle( z, DEG2RAD( d->yaw ), yaw );
	for ( r = 0; r < 3; r++ ) {
		ax[r * 4 + 0] = axis[0][r] * d->scale;
		ax[r * 4 + 1] = axis[1][r] * d->scale;
		ax[r * 4 + 2] = axis[2][r] * d->scale;
		ax[r * 4 + 3] = origin[r] + d->offset[r];
	}
	M34_Mul( ax, yaw, e );
}

/*
==============================================================================
drawing
==============================================================================
*/

static void CG_SkelAdd( refEntity_t *re, const float *worldMats, int numJoints, entityState_t *state, int team ) {
	static float mats[SKEL_MAX_JOINTS * 12];
	int i;

	/* matrices relative to the entity origin: lighting, fog and sorting use it */
	for ( i = 0; i < numJoints; i++ ) {
		memcpy( mats + i * 12, worldMats + i * 12, 12 * sizeof( float ) );
		mats[i * 12 + 3] -= re->origin[0];
		mats[i * 12 + 7] -= re->origin[1];
		mats[i * 12 + 11] -= re->origin[2];
	}
	AxisClear( re->axis );
	re->reType = RT_MODEL;
	if ( state->powerups & ( 1 << PW_INVIS ) ) {
		re->customShader = cgs.media.invisShader;
		trap_Phys_R_AddSkeletalEntity( re, mats, numJoints );
		return;
	}
	trap_Phys_R_AddSkeletalEntity( re, mats, numJoints );
	if ( state->powerups & ( 1 << PW_QUAD ) ) {
		re->customShader = team == TEAM_RED ? cgs.media.redQuadShader : cgs.media.quadShader;
		trap_Phys_R_AddSkeletalEntity( re, mats, numJoints );
	}
	if ( state->powerups & ( 1 << PW_REGEN ) ) {
		if ( ( ( cg.time / 100 ) % 10 ) == 1 ) {
			re->customShader = cgs.media.regenShader;
			trap_Phys_R_AddSkeletalEntity( re, mats, numJoints );
		}
	}
	if ( state->powerups & ( 1 << PW_BATTLESUIT ) ) {
		re->customShader = cgs.media.battleSuitShader;
		trap_Phys_R_AddSkeletalEntity( re, mats, numJoints );
	}
	re->customShader = 0;
}

/*
==============================================================================
ragdolls
==============================================================================
*/

static void CG_SkelFreeRagdoll( skelRagdoll_t *r ) {
	int i;

	if ( !r->active ) {
		return;
	}
	if ( cgPhys.world ) {
		for ( i = 0; i < SKEL_MAX_BONES; i++ ) {
			if ( r->bodies[i] ) {
				trap_Phys_BodyDestroy( r->bodies[i] );
			}
		}
	}
	memset( r, 0, sizeof( *r ) );
	cgPhys.ragdolls--;
}

static skelRagdoll_t *CG_SkelNewRagdoll( void ) {
	skelRagdoll_t *oldest = NULL;
	int i;

	for ( i = 0; i < SKEL_MAX_RAGDOLLS; i++ ) {
		if ( !ragdolls[i].active ) {
			return &ragdolls[i];
		}
		if ( !oldest || ragdolls[i].startTime < oldest->startTime ) {
			oldest = &ragdolls[i];
		}
	}
	CG_SkelFreeRagdoll( oldest );
	return oldest;
}

/*
==================
CG_SkelStartRagdoll

From the client's last living world pose: capsules per bone, joints, the
dying player's velocity.
==================
*/
static skelRagdoll_t *CG_SkelStartRagdoll( centity_t *cent, int defIndex ) {
	const skelDef_t *d = &skelDefs[defIndex];
	int clientNum = cent->currentState.clientNum;
	float (*pose)[12] = lastPose[clientNum];
	oaxPhysRagdollBone_t bones[SKEL_MAX_BONES];
	oaxPhysRagdollDef_t rd;
	skelRagdoll_t *r;
	vec3_t shift;
	int i, n;

	if ( !CG_PhysActive() || !d->numBones || lastPoseDef[clientNum] != defIndex + 1
		|| cg.time - lastPoseTime[clientNum] > 500 ) {
		return NULL;
	}
	/* the pose may be a frame old and the player teleported since: move it
	   to where the body is now */
	VectorSubtract( cent->lerpOrigin, lastPoseOrigin[clientNum], shift );
	for ( i = 0; i < d->numJoints; i++ ) {
		pose[i][3] += shift[0];
		pose[i][7] += shift[1];
		pose[i][11] += shift[2];
	}
	VectorCopy( cent->lerpOrigin, lastPoseOrigin[clientNum] );
	memset( &rd, 0, sizeof( rd ) );
	VectorCopy( cent->currentState.pos.trDelta, rd.velocity );
	rd.groupIndex = -100 - clientNum;
	rd.categoryBits = 4u;
	rd.linearDamping = 0.05f;
	rd.angularDamping = 0.3f;
	rd.friction = 0.8f;
	for ( i = 0; i < d->numBones; i++ ) {
		const skelBone_t *sb = &d->bones[i];
		oaxPhysRagdollBone_t *b = &bones[i];
		memset( b, 0, sizeof( *b ) );
		b->parent = sb->parent;
		M34_Origin( pose[sb->joint], b->head );
		if ( sb->tailJoint >= 0 ) {
			M34_Origin( pose[sb->tailJoint], b->tail );
		} else {
			vec3_t ax;
			M34_Column( pose[sb->joint], 1, ax );		/* +y, scaled with the model */
			VectorMA( b->head, sb->tailLen, ax, b->tail );
		}
		b->radius = sb->radius * d->scale;
		b->density = 1000.0f;
		b->jointType = PHYS_RAGDOLL_BALL;
		b->coneAngle = sb->cone;
		b->lower = sb->lo;
		b->upper = sb->hi;
		b->friction = 30.0f;
	}
	r = CG_SkelNewRagdoll();
	memset( r, 0, sizeof( *r ) );
	n = trap_Phys_RagdollCreate( cgPhys.world, &rd, bones, d->numBones, r->bodies );
	if ( n != d->numBones ) {
		for ( i = 0; i < n; i++ ) {
			trap_Phys_BodyDestroy( r->bodies[i] );
		}
		memset( r, 0, sizeof( *r ) );
		return NULL;
	}
	/* a dead body never balances: tip the upper bones backward a little */
	{
		vec3_t fwd, push;
		AngleVectors( cent->lerpAngles, fwd, NULL, NULL );
		for ( i = 0; i < n; i++ ) {
			if ( bones[i].head[2] > cent->lerpOrigin[2] ) {
				VectorMA( rd.velocity, -60.0f - 40.0f * random(), fwd, push );
				push[2] = rd.velocity[2];
				trap_Phys_BodySetVelocity( r->bodies[i], push, NULL );
			}
		}
	}
	r->active = qtrue;
	r->def = defIndex;
	r->clientNum = clientNum;
	r->ownerEnt = cent->currentState.number;
	r->startTime = r->lastDrawn = cg.time;
	cgPhys.ragdolls++;
	/* each joint relative to its bone's body, which starts unrotated at the
	   bone's midpoint (phys_ragdoll.c) */
	for ( i = 0; i < d->numJoints; i++ ) {
		int b = d->jointBone[i];
		vec3_t mid;
		VectorAdd( bones[b].head, bones[b].tail, mid );
		VectorScale( mid, 0.5f, mid );
		memcpy( r->jointLocal[i], pose[i], sizeof( r->jointLocal[i] ) );
		r->jointLocal[i][3] -= mid[0];
		r->jointLocal[i][7] -= mid[1];
		r->jointLocal[i][11] -= mid[2];
	}
	return r;
}

static qboolean CG_SkelRagdollPose( skelRagdoll_t *r, float *mats, vec3_t pelvis ) {
	const skelDef_t *d = &skelDefs[r->def];
	oaxPhysBodyState_t st[SKEL_MAX_BONES];
	float body[SKEL_MAX_BONES][12];
	int i;

	if ( trap_Phys_BodyGetStates( r->bodies, d->numBones, st ) != d->numBones ) {
		return qfalse;
	}
	for ( i = 0; i < d->numBones; i++ ) {
		M34_FromQuat( st[i].quat, st[i].origin, body[i] );
	}
	for ( i = 0; i < d->numJoints; i++ ) {
		M34_Mul( body[d->jointBone[i]], r->jointLocal[i], mats + i * 12 );
	}
	VectorCopy( st[0].origin, pelvis );
	return qtrue;
}

static skelRagdoll_t *CG_SkelFindRagdoll( centity_t *cent ) {
	int num = cent->currentState.number, i;
	skelRagdoll_t *best = NULL;

	for ( i = 0; i < SKEL_MAX_RAGDOLLS; i++ ) {
		if ( ragdolls[i].active && ragdolls[i].ownerEnt == num ) {
			return &ragdolls[i];
		}
	}
	/* a body-queue corpse takes over the ragdoll of its client's last death,
	   once that player is alive again */
	if ( num >= MAX_CLIENTS ) {
		for ( i = 0; i < SKEL_MAX_RAGDOLLS; i++ ) {
			skelRagdoll_t *r = &ragdolls[i];
			if ( r->active && r->clientNum == cent->currentState.clientNum && r->ownerEnt < MAX_CLIENTS
				&& !( cg_entities[r->ownerEnt].currentState.eFlags & EF_DEAD ) && ( !best || r->startTime > best->startTime ) ) {
				best = r;
			}
		}
		if ( best ) {
			best->ownerEnt = num;
			best->sinkSet = qfalse;
		}
	}
	return best;
}

/*
==================
CG_SkelPlayer

CG_Player for a skeletal model. Fills `torso` with where the weapon goes
(the right hand, aimed like the torso) and returns qtrue when the caller
should add the weapon and powerups (a living player).
==================
*/
qboolean CG_SkelPlayer( centity_t *cent, clientInfo_t *ci, refEntity_t *legs, refEntity_t *torso, int renderfx, float shadowPlane ) {
	const skelDef_t *d;
	static float model[SKEL_MAX_JOINTS * 12];
	static float world[SKEL_MAX_JOINTS * 12];
	float e[12];
	refEntity_t re;
	vec3_t legsAngles, torsoAngles, headAngles, pelvis;
	int i, clientNum = cent->currentState.clientNum;

	if ( ci->oaxSkel < 1 || ci->oaxSkel > numSkelDefs ) {
		return qfalse;
	}
	d = &skelDefs[ci->oaxSkel - 1];
	memset( &re, 0, sizeof( re ) );
	re.hModel = d->model;
	re.customSkin = d->skin;
	re.renderfx = renderfx | RF_LIGHTING_ORIGIN;
	re.shadowPlane = shadowPlane;
	VectorCopy( cent->lerpOrigin, re.origin );
	VectorCopy( cent->lerpOrigin, re.lightingOrigin );
	VectorCopy( re.origin, re.oldorigin );

	if ( cent->currentState.eFlags & EF_DEAD ) {
		skelRagdoll_t *r = CG_SkelFindRagdoll( cent );
		if ( !r && cent->currentState.number < MAX_CLIENTS ) {
			r = CG_SkelStartRagdoll( cent, ci->oaxSkel - 1 );
		}
		if ( r && r->def == ci->oaxSkel - 1 && CG_SkelRagdollPose( r, world, pelvis ) ) {
			r->lastDrawn = cg.time;
			/* the corpse sinks: so does its ragdoll */
			if ( cent->currentState.number >= MAX_CLIENTS ) {
				if ( !r->sinkSet ) {
					r->sinkSet = qtrue;
					r->sinkBase = cent->lerpOrigin[2];
				}
				for ( i = 0; i < d->numJoints; i++ ) {
					world[i * 12 + 11] += cent->lerpOrigin[2] - r->sinkBase;
				}
				pelvis[2] += cent->lerpOrigin[2] - r->sinkBase;
			}
			VectorCopy( pelvis, re.origin );
			VectorCopy( pelvis, re.lightingOrigin );
			CG_SkelAdd( &re, world, d->numJoints, &cent->currentState, ci->team );
			return qfalse;
		}
		/* no physics: lying on its back where it fell */
		{
			vec3_t ax[3], ang;
			float lie[12], w[12], back[12];
			VectorSet( ang, 0, cent->lerpAngles[YAW], 0 );
			AnglesToAxis( ang, ax );
			CG_SkelWorldMatrix( d, cent->lerpOrigin, ax, w );
			M34_AxisAngle( d->swingAxis, -M_PI * 0.5f, back );
			memcpy( lie, w, sizeof( lie ) );
			M34_Mul( w, back, lie );
			lie[11] += 4.0f;
			memcpy( model, d->bind, d->numJoints * 12 * sizeof( float ) );
			for ( i = 0; i < d->numJoints; i++ ) {
				M34_Mul( lie, model + i * 12, world + i * 12 );
			}
			CG_SkelAdd( &re, world, d->numJoints, &cent->currentState, ci->team );
		}
		return qfalse;
	}

	/* alive: pose, remember it for a ragdoll, draw */
	vectoangles( legs->axis[0], legsAngles );
	vectoangles( torso->axis[0], torsoAngles );
	VectorCopy( cent->lerpAngles, headAngles );
	CG_SkelPose( d, cent, legsAngles, torsoAngles, headAngles, model );
	CG_SkelWorldMatrix( d, cent->lerpOrigin, legs->axis, e );
	for ( i = 0; i < d->numJoints; i++ ) {
		M34_Mul( e, model + i * 12, world + i * 12 );
	}
	if ( cent->currentState.number == clientNum ) {
		memcpy( lastPose[clientNum], world, d->numJoints * 12 * sizeof( float ) );
		lastPoseTime[clientNum] = cg.time;
		lastPoseDef[clientNum] = ci->oaxSkel;
		VectorCopy( cent->lerpOrigin, lastPoseOrigin[clientNum] );
	}
	CG_SkelAdd( &re, world, d->numJoints, &cent->currentState, ci->team );

	/* the weapon in the right hand, aimed along the torso */
	VectorCopy( cent->lerpOrigin, torso->origin );
	if ( d->role[ROLE_HAND_R] >= 0 ) {
		M34_Origin( world + d->role[ROLE_HAND_R] * 12, torso->origin );
	}
	VectorCopy( cent->lerpOrigin, torso->lightingOrigin );
	torso->renderfx = renderfx | RF_LIGHTING_ORIGIN;
	torso->shadowPlane = shadowPlane;
	torso->hModel = 0;
	if ( d->role[ROLE_HEAD] >= 0 ) {
		M34_Origin( world + d->role[ROLE_HEAD] * 12, cent->headpos );
	}
	return qtrue;
}

/* EV_GIB_PLAYER: the gibs replace the ragdoll */
void CG_SkelGibbed( centity_t *cent ) {
	int i;

	for ( i = 0; i < SKEL_MAX_RAGDOLLS; i++ ) {
		if ( ragdolls[i].active && ragdolls[i].ownerEnt == cent->currentState.number ) {
			CG_SkelFreeRagdoll( &ragdolls[i] );
		}
	}
}

void CG_SkelInit( void ) {
	memset( ragdolls, 0, sizeof( ragdolls ) );
	memset( lastPoseTime, 0, sizeof( lastPoseTime ) );
}

/* ragdolls nobody draws any more (the corpse is gone) go; the newest one's
   state is published for tests (cg_skel_*) */
void CG_SkelFrame( void ) {
	skelRagdoll_t *newest = NULL;
	oaxPhysBodyState_t st;
	int i;

	for ( i = 0; i < SKEL_MAX_RAGDOLLS; i++ ) {
		skelRagdoll_t *r = &ragdolls[i];
		if ( r->active && ( cg.time - r->lastDrawn > SKEL_RAGDOLL_IDLE || cg.time - r->startTime > SKEL_RAGDOLL_LIFE
			|| cg.time < r->startTime ) ) {
			CG_SkelFreeRagdoll( r );
		}
	}
	for ( i = 0; i < SKEL_MAX_RAGDOLLS; i++ ) {
		if ( ragdolls[i].active && ( !newest || ragdolls[i].startTime > newest->startTime ) ) {
			newest = &ragdolls[i];
		}
	}
	if ( newest && trap_Phys_BodyGetState( newest->bodies[0], &st ) ) {
		BG_OAXDebugSet( "cg_skel_ragdoll", va( "%.1f %.1f %.1f %d %d", st.origin[0], st.origin[1], st.origin[2],
			( st.flags & PHYS_STATE_AWAKE ) ? 1 : 0, newest->ownerEnt ) );
	}
}
