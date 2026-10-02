/*
===========================================================================
g_oax_phys.c: server-side (authoritative) physics.

G_PhysWorld() is the level's authoritative Box3D world, made on first use
(nothing is created on a level that never asks, so stock play is
untouched): the map's solid and player-clip brushes plus its patches as
static collision, the level's gravity, 16 ms ticks. G_OAXPhysFrame steps
it by the server frame time. Vehicles (phase 8) build on this.

`physscene <tag> <workers> <ticks> [perturb]` (server console) runs the
phase 6 determinism scene in a world of its own: box stacks, a few hundred
debris bodies of every shape, ragdolls, a jointed bridge, a wheeled cart,
a piston, ropes, welds, a kinematic sweeper, explosions and ray batches,
stepped tick by tick with the per-tick world hash recorded. Results are
debug values (phys_scene_<tag>_*), read by the physics-determinism test:
the hashes must be identical for every worker count and on every build,
and `perturb` (one debris body nudged by a thousandth of a unit) must
change them.
===========================================================================
*/
#include "g_local.h"
#include "bg_oax_phys.h"

#define SCENE_TICKS_PER_FRAME	25
#define SCENE_MAX_TICKS			1200
#define SCENE_HASHES_PER_VALUE	100

static int		g_physWorld;
static int		g_physLastTime;

typedef struct {
	qboolean	active;
	char		tag[32];
	int			world;
	int			ticks, tick;
	int			workers;
	unsigned	chain;
	unsigned	hashes[SCENE_MAX_TICKS];
	unsigned	seed;
	int			sweeper;
	int			cartMotor[2];
	int			piston;
	float		stepMs;
	int			rayHits;
} physScene_t;

static physScene_t	scene;

/*
==================
G_PhysWorld

The level's authoritative world (0 if the engine has no physics).
==================
*/
int G_PhysWorld( void ) {
	oaxPhysWorldDef_t def;

	if ( g_physWorld ) {
		return g_physWorld;
	}
	if ( !BG_OAXFeature( "physics" ) ) {
		return 0;
	}
	BG_PhysWorldDefInit( &def, g_gravity.value );
	g_physWorld = trap_Phys_WorldCreate( &def );
	if ( g_physWorld ) {
		trap_Phys_WorldAddBSP( g_physWorld, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, PHYS_BSP_PATCHES, NULL );
		g_physLastTime = level.time;
	}
	return g_physWorld;
}

/*
==============================================================================
the determinism scene
==============================================================================
*/

static float Scene_Rand( void ) {
	scene.seed = scene.seed * 1103515245u + 12345u;
	return (float)( ( scene.seed >> 8 ) & 0xffff ) / 65535.0f;
}

static float Scene_Range( float lo, float hi ) {
	return lo + ( hi - lo ) * Scene_Rand();
}

static int Scene_Body( int type, const vec3_t origin, const vec3_t angles, const vec3_t velocity ) {
	oaxPhysBodyDef_t bd;

	BG_PhysBodyDefInit( &bd, type );
	VectorCopy( origin, bd.origin );
	BG_QuatFromAngles( angles, bd.quat );
	if ( velocity ) {
		VectorCopy( velocity, bd.velocity );
	}
	return trap_Phys_BodyCreate( scene.world, &bd );
}

static void Scene_Box( int body, float hx, float hy, float hz, float density ) {
	oaxPhysShapeDef_t sd;

	BG_PhysShapeDefInit( &sd, PHYS_SHAPE_BOX, density );
	sd.params[0] = hx;
	sd.params[1] = hy;
	sd.params[2] = hz;
	trap_Phys_BodyAddShape( body, &sd, NULL, 0, NULL, 0 );
}

static void Scene_Stacks( void ) {
	int s, row, col, b;
	vec3_t org, ang;

	VectorClear( ang );
	for ( s = 0; s < 4; s++ ) {
		float bx = ( s & 1 ) ? 300.0f : -300.0f;
		float by = ( s & 2 ) ? 300.0f : -300.0f;
		for ( row = 0; row < 8; row++ ) {
			for ( col = 0; col < 8 - row; col++ ) {
				org[0] = bx + ( col - ( 8 - row ) * 0.5f + 0.5f ) * 17.0f;
				org[1] = by;
				org[2] = 8.0f + row * 16.0f;
				b = Scene_Body( PHYS_BODY_DYNAMIC, org, ang, NULL );
				Scene_Box( b, 8.0f, 8.0f, 8.0f, 400.0f );
			}
		}
	}
}

static void Scene_Debris( int count, float perturb ) {
	int i, j, b;
	vec3_t org, ang, vel;
	oaxPhysShapeDef_t sd;
	float pts[12 * 3];

	for ( i = 0; i < count; i++ ) {
		int kind = i % 4;
		org[0] = -640.0f + ( i % 20 ) * 64.0f + Scene_Range( -8, 8 );
		org[1] = -640.0f + ( ( i / 20 ) % 20 ) * 64.0f + Scene_Range( -8, 8 );
		org[2] = 300.0f + ( i / 400 ) * 60.0f + Scene_Range( 0, 200 );
		if ( i == 0 ) {
			org[0] += perturb;
		}
		ang[0] = Scene_Range( 0, 360 );
		ang[1] = Scene_Range( 0, 360 );
		ang[2] = Scene_Range( 0, 360 );
		vel[0] = Scene_Range( -100, 100 );
		vel[1] = Scene_Range( -100, 100 );
		vel[2] = Scene_Range( -50, 50 );
		b = Scene_Body( PHYS_BODY_DYNAMIC, org, ang, vel );
		switch ( kind ) {
		case 0:
			Scene_Box( b, Scene_Range( 2, 8 ), Scene_Range( 2, 8 ), Scene_Range( 2, 8 ), 500.0f );
			break;
		case 1:
			BG_PhysShapeDefInit( &sd, PHYS_SHAPE_SPHERE, 500.0f );
			sd.params[3] = Scene_Range( 2, 7 );
			sd.restitution = 0.3f;
			trap_Phys_BodyAddShape( b, &sd, NULL, 0, NULL, 0 );
			break;
		case 2:
			BG_PhysShapeDefInit( &sd, PHYS_SHAPE_CAPSULE, 500.0f );
			sd.params[0] = -Scene_Range( 2, 8 );
			sd.params[3] = Scene_Range( 2, 8 );
			sd.params[6] = Scene_Range( 1.5f, 4 );
			trap_Phys_BodyAddShape( b, &sd, NULL, 0, NULL, 0 );
			break;
		default:
			BG_PhysShapeDefInit( &sd, PHYS_SHAPE_HULL, 500.0f );
			for ( j = 0; j < 12 * 3; j++ ) {
				pts[j] = Scene_Range( -7, 7 );
			}
			if ( !trap_Phys_BodyAddShape( b, &sd, pts, 12, NULL, 0 ) ) {
				Scene_Box( b, 4, 4, 4, 500.0f );
			}
			/* a compound: a second shape off to the side on every other hull */
			if ( ( i / 4 ) & 1 ) {
				BG_PhysShapeDefInit( &sd, PHYS_SHAPE_SPHERE, 500.0f );
				sd.params[0] = 8.0f;
				sd.params[3] = 3.0f;
				trap_Phys_BodyAddShape( b, &sd, NULL, 0, NULL, 0 );
			}
			break;
		}
	}
}

static void Scene_Ragdolls( int count ) {
	oaxPhysRagdollBone_t bones[PHYS_HUMAN_BONES];
	oaxPhysRagdollDef_t rd;
	int bodies[PHYS_HUMAN_BONES];
	vec3_t feet;
	int i;

	for ( i = 0; i < count; i++ ) {
		feet[0] = -480.0f + ( i % 4 ) * 320.0f;
		feet[1] = -160.0f + ( i / 4 ) * 110.0f;
		feet[2] = 120.0f + ( i % 3 ) * 80.0f;
		BG_PhysHumanBones( feet, i * 37.0f, 1.0f, bones );
		memset( &rd, 0, sizeof( rd ) );
		rd.velocity[0] = Scene_Range( -150, 150 );
		rd.velocity[1] = Scene_Range( -150, 150 );
		rd.velocity[2] = Scene_Range( 0, 200 );
		rd.groupIndex = -1 - i;
		rd.angularDamping = 0.1f;
		trap_Phys_RagdollCreate( scene.world, &rd, bones, PHYS_HUMAN_BONES, bodies );
	}
}

static int Scene_Joint( int type, int a, int b, const vec3_t anchorA, const vec3_t anchorB, oaxPhysJointDef_t *jd ) {
	jd->type = type;
	jd->bodyA = a;
	jd->bodyB = b;
	VectorCopy( anchorA, jd->frameA );
	VectorCopy( anchorB, jd->frameB );
	if ( !jd->frameA[6] && !jd->frameA[3] && !jd->frameA[4] && !jd->frameA[5] ) jd->frameA[6] = 1.0f;
	if ( !jd->frameB[6] && !jd->frameB[3] && !jd->frameB[4] && !jd->frameB[5] ) jd->frameB[6] = 1.0f;
	return trap_Phys_JointCreate( scene.world, jd );
}

static void Scene_Contraptions( void ) {
	oaxPhysJointDef_t jd;
	vec3_t org, ang, a, b;
	int i, prev, link, chassis, wheel, base, rod;
	/* frame quaternion turning z (the hinge / spin axis) onto y */
	static const float zToY[4] = { -0.70710678f, 0.0f, 0.0f, 0.70710678f };

	VectorClear( ang );

	/* a bridge of 16 planks on revolute hinges, anchored to the ground at both ends */
	prev = 0;
	for ( i = 0; i < 16; i++ ) {
		VectorSet( org, -400.0f + i * 34.0f, 700.0f, 300.0f );
		link = Scene_Body( PHYS_BODY_DYNAMIC, org, ang, NULL );
		Scene_Box( link, 16.0f, 24.0f, 2.0f, 600.0f );
		memset( &jd, 0, sizeof( jd ) );
		Vector4Copy( zToY, jd.frameA + 3 );
		Vector4Copy( zToY, jd.frameB + 3 );
		if ( prev ) {
			VectorSet( a, 17.0f, 0, 0 );
		} else {
			VectorSet( a, org[0] - 17.0f, org[1], org[2] );
		}
		VectorSet( b, -17.0f, 0, 0 );
		Scene_Joint( PHYS_JOINT_REVOLUTE, prev, link, a, b, &jd );
		prev = link;
	}
	memset( &jd, 0, sizeof( jd ) );
	Vector4Copy( zToY, jd.frameA + 3 );
	Vector4Copy( zToY, jd.frameB + 3 );
	VectorSet( a, org[0] + 17.0f, org[1], org[2] );
	VectorSet( b, 17.0f, 0, 0 );
	Scene_Joint( PHYS_JOINT_REVOLUTE, 0, prev, a, b, &jd );

	/* a spherical chain hanging from the ceiling */
	prev = 0;
	for ( i = 0; i < 10; i++ ) {
		oaxPhysShapeDef_t sd;
		VectorSet( org, 600.0f, 600.0f - i * 14.0f, 700.0f );
		link = Scene_Body( PHYS_BODY_DYNAMIC, org, ang, NULL );
		BG_PhysShapeDefInit( &sd, PHYS_SHAPE_CAPSULE, 800.0f );
		sd.params[1] = -5.0f;
		sd.params[4] = 5.0f;
		sd.params[6] = 2.5f;
		trap_Phys_BodyAddShape( link, &sd, NULL, 0, NULL, 0 );
		memset( &jd, 0, sizeof( jd ) );
		jd.flags = PHYS_JF_LIMIT;
		jd.coneAngle = 1.2f;
		if ( prev ) {
			VectorSet( a, 0, -7.0f, 0 );
		} else {
			VectorSet( a, org[0], org[1] + 7.0f, org[2] );
		}
		VectorSet( b, 0, 7.0f, 0 );
		Scene_Joint( PHYS_JOINT_SPHERICAL, prev, link, a, b, &jd );
		prev = link;
	}

	/* a cart: chassis + 4 wheels on wheel joints (suspension along z, spin about y), rear motors */
	VectorSet( org, -600.0f, -300.0f, 60.0f );
	chassis = Scene_Body( PHYS_BODY_DYNAMIC, org, ang, NULL );
	Scene_Box( chassis, 40.0f, 20.0f, 6.0f, 300.0f );
	for ( i = 0; i < 4; i++ ) {
		oaxPhysShapeDef_t sd;
		float wx = ( i & 1 ) ? 30.0f : -30.0f;
		float wy = ( i & 2 ) ? 26.0f : -26.0f;
		VectorSet( a, org[0] + wx, org[1] + wy, org[2] - 14.0f );
		wheel = Scene_Body( PHYS_BODY_DYNAMIC, a, ang, NULL );
		BG_PhysShapeDefInit( &sd, PHYS_SHAPE_CAPSULE, 400.0f );
		sd.params[1] = -3.0f;
		sd.params[4] = 3.0f;
		sd.params[6] = 9.0f;
		sd.friction = 1.0f;
		trap_Phys_BodyAddShape( wheel, &sd, NULL, 0, NULL, 0 );
		memset( &jd, 0, sizeof( jd ) );
		/* frame A: x axis (suspension) turned onto -z; frame B: z (spin) onto y */
		VectorSet( jd.frameA, wx, wy, -14.0f );
		jd.frameA[3] = 0.0f; jd.frameA[4] = 0.70710678f; jd.frameA[5] = 0.0f; jd.frameA[6] = 0.70710678f;
		Vector4Copy( zToY, jd.frameB + 3 );
		jd.flags = PHYS_JF_SPRING | PHYS_JF_LIMIT | ( wx < 0 ? PHYS_JF_MOTOR : 0 );
		jd.hertz = 4.0f;
		jd.dampingRatio = 0.7f;
		jd.lower = -6.0f;
		jd.upper = 6.0f;
		jd.motorSpeed = 6.0f;
		jd.maxMotorForce = 40000.0f;
		jd.type = PHYS_JOINT_WHEEL;
		jd.bodyA = chassis;
		jd.bodyB = wheel;
		link = trap_Phys_JointCreate( scene.world, &jd );
		if ( wx < 0 ) {
			scene.cartMotor[( i & 2 ) ? 1 : 0] = link;
		}
	}

	/* a piston: prismatic joint pushing a block along x, with a motor */
	VectorSet( org, 0.0f, -700.0f, 40.0f );
	base = Scene_Body( PHYS_BODY_STATIC, org, ang, NULL );
	Scene_Box( base, 20.0f, 20.0f, 20.0f, 0.0f );
	VectorSet( a, org[0] + 50.0f, org[1], org[2] );
	rod = Scene_Body( PHYS_BODY_DYNAMIC, a, ang, NULL );
	Scene_Box( rod, 20.0f, 10.0f, 10.0f, 400.0f );
	memset( &jd, 0, sizeof( jd ) );
	VectorSet( a, 20.0f, 0, 0 );
	VectorSet( b, -30.0f, 0, 0 );
	jd.flags = PHYS_JF_LIMIT | PHYS_JF_MOTOR;
	jd.lower = 0.0f;
	jd.upper = 120.0f;
	jd.motorSpeed = 60.0f;
	jd.maxMotorForce = 300000.0f;
	scene.piston = Scene_Joint( PHYS_JOINT_PRISMATIC, base, rod, a, b, &jd );

	/* ropes: distance joints holding boxes under the ceiling, one springy */
	for ( i = 0; i < 4; i++ ) {
		VectorSet( org, -200.0f + i * 60.0f, 400.0f, 500.0f );
		link = Scene_Body( PHYS_BODY_DYNAMIC, org, ang, NULL );
		Scene_Box( link, 6.0f, 6.0f, 6.0f, 600.0f );
		memset( &jd, 0, sizeof( jd ) );
		VectorSet( a, org[0] + 20.0f, org[1], 760.0f );
		VectorSet( b, 0, 0, 6.0f );
		jd.length = 260.0f - org[2] + 500.0f;
		jd.flags = ( i & 1 ) ? PHYS_JF_SPRING : 0;
		jd.hertz = 2.0f;
		jd.dampingRatio = 0.3f;
		Scene_Joint( PHYS_JOINT_DISTANCE, 0, link, a, b, &jd );
	}

	/* a welded pair (soft weld) dropped on the ramp */
	VectorSet( org, 760.0f, -380.0f, 250.0f );
	link = Scene_Body( PHYS_BODY_DYNAMIC, org, ang, NULL );
	Scene_Box( link, 10.0f, 10.0f, 10.0f, 500.0f );
	VectorSet( a, 790.0f, -380.0f, 250.0f );
	prev = Scene_Body( PHYS_BODY_DYNAMIC, a, ang, NULL );
	Scene_Box( prev, 10.0f, 10.0f, 10.0f, 500.0f );
	memset( &jd, 0, sizeof( jd ) );
	VectorSet( a, 15.0f, 0, 0 );
	VectorSet( b, -15.0f, 0, 0 );
	jd.hertz = 8.0f;
	jd.dampingRatio = 0.5f;
	jd.angularHertz = 8.0f;
	jd.angularDampingRatio = 0.5f;
	Scene_Joint( PHYS_JOINT_WELD, link, prev, a, b, &jd );

	/* a kinematic sweeper arm moved by target transforms each tick */
	VectorSet( org, 0.0f, 0.0f, 24.0f );
	scene.sweeper = Scene_Body( PHYS_BODY_KINEMATIC, org, ang, NULL );
	Scene_Box( scene.sweeper, 300.0f, 6.0f, 20.0f, 0.0f );
}

static void Scene_Publish( void ) {
	char buf[1024];
	oaxPhysStats_t st;
	int i, part;

	for ( part = 0; part * SCENE_HASHES_PER_VALUE < scene.tick; part++ ) {
		buf[0] = '\0';
		for ( i = part * SCENE_HASHES_PER_VALUE; i < scene.tick && i < ( part + 1 ) * SCENE_HASHES_PER_VALUE; i++ ) {
			Q_strcat( buf, sizeof( buf ), va( "%08x ", scene.hashes[i] ) );
		}
		BG_OAXDebugSet( va( "phys_scene_%s_h%d", scene.tag, part ), buf );
	}
	trap_Phys_WorldStats( scene.world, &st );
	BG_OAXDebugSet( va( "phys_scene_%s_chain", scene.tag ), va( "%08x", scene.chain ) );
	BG_OAXDebugSetInt( va( "phys_scene_%s_ticks", scene.tag ), scene.tick );
	BG_OAXDebugSetInt( va( "phys_scene_%s_bodies", scene.tag ), st.bodies );
	BG_OAXDebugSetInt( va( "phys_scene_%s_awake", scene.tag ), st.awakeBodies );
	BG_OAXDebugSetInt( va( "phys_scene_%s_joints", scene.tag ), st.joints );
	BG_OAXDebugSetInt( va( "phys_scene_%s_contacts", scene.tag ), st.contacts );
	BG_OAXDebugSetInt( va( "phys_scene_%s_workers", scene.tag ), st.workerCount );
	BG_OAXDebugSetInt( va( "phys_scene_%s_rayhits", scene.tag ), scene.rayHits );
	BG_OAXDebugSetFloat( va( "phys_scene_%s_step_ms", scene.tag ), scene.stepMs );
	BG_OAXDebugSetInt( va( "phys_scene_%s_done", scene.tag ), 1 );
}

static void Scene_Tick( void ) {
	oaxPhysRay_t rays[16];
	oaxPhysHit_t hits[16];
	oaxPhysStats_t st;
	vec3_t org, ang, q;
	float quat[4];
	int i;
	unsigned h;

	/* the sweeper turns half a degree a tick */
	VectorSet( org, 0.0f, 0.0f, 24.0f );
	VectorSet( ang, 0.0f, scene.tick * 0.5f, 0.0f );
	BG_QuatFromAngles( ang, quat );
	trap_Phys_BodySetTarget( scene.sweeper, org, quat, 0.016f );

	/* explosions */
	if ( scene.tick == 120 || scene.tick == 300 || scene.tick == 480 ) {
		VectorSet( q, -300.0f + scene.tick, 300.0f - scene.tick * 0.5f, 16.0f );
		trap_Phys_WorldExplode( scene.world, q, 300.0f, 100.0f, 8000.0f, 0 );
	}
	/* reverse the cart and the piston now and then */
	if ( scene.tick % 200 == 100 ) {
		float sp = ( scene.tick / 200 ) & 1 ? 6.0f : -6.0f;
		trap_Phys_JointSetParam( scene.cartMotor[0], PHYS_JP_MOTOR_SPEED, sp );
		trap_Phys_JointSetParam( scene.cartMotor[1], PHYS_JP_MOTOR_SPEED, sp );
		trap_Phys_JointSetParam( scene.piston, PHYS_JP_MOTOR_SPEED, -sp * 10.0f );
	}

	trap_Phys_WorldStep( scene.world, -1 );
	trap_Phys_WorldStats( scene.world, &st );
	scene.stepMs += st.lastStepMs;
	h = (unsigned)trap_Phys_WorldHash( scene.world );

	/* every 30 ticks a batch of rays goes into the hash too */
	if ( scene.tick % 30 == 0 ) {
		for ( i = 0; i < 16; i++ ) {
			VectorSet( rays[i].start, -900.0f + i * 120.0f, -900.0f, 400.0f );
			VectorSet( rays[i].end, 900.0f - i * 120.0f, 900.0f, 0.0f );
			rays[i].maskBits = 0;
			rays[i].ignoreBody = 0;
		}
		scene.rayHits += trap_Phys_RaycastBatch( scene.world, rays, 16, hits );
		for ( i = 0; i < 16; i++ ) {
			h = ( h ^ (unsigned)hits[i].body ) * 16777619u;
			h = ( h ^ *(unsigned *)&hits[i].fraction ) * 16777619u;
		}
	}

	scene.hashes[scene.tick] = h;
	scene.chain = ( scene.chain ^ h ) * 16777619u;
	scene.tick++;
}

static void Scene_Frame( void ) {
	int i;

	if ( !scene.active ) {
		return;
	}
	for ( i = 0; i < SCENE_TICKS_PER_FRAME && scene.tick < scene.ticks; i++ ) {
		Scene_Tick();
	}
	BG_OAXDebugSetInt( va( "phys_scene_%s_progress", scene.tag ), scene.tick );
	if ( scene.tick >= scene.ticks ) {
		Scene_Publish();
		trap_Phys_WorldDestroy( scene.world );
		scene.active = qfalse;
	}
}

/*
==================
G_OAXPhysScene_f

physscene <tag> <workers> <ticks> [perturb]
==================
*/
void G_OAXPhysScene_f( void ) {
	char arg[64];
	oaxPhysWorldDef_t def;
	float perturb;

	if ( !BG_OAXFeature( "physics" ) ) {
		G_Printf( "physscene: the engine has no physics\n" );
		return;
	}
	if ( trap_Argc() < 4 ) {
		G_Printf( "usage: physscene <tag> <workers> <ticks> [perturb]\n" );
		return;
	}
	if ( scene.active ) {
		trap_Phys_WorldDestroy( scene.world );
	}
	memset( &scene, 0, sizeof( scene ) );
	trap_Argv( 1, scene.tag, sizeof( scene.tag ) );
	trap_Argv( 2, arg, sizeof( arg ) );
	scene.workers = atoi( arg );
	trap_Argv( 3, arg, sizeof( arg ) );
	scene.ticks = atoi( arg );
	if ( scene.ticks < 1 ) scene.ticks = 1;
	if ( scene.ticks > SCENE_MAX_TICKS ) scene.ticks = SCENE_MAX_TICKS;
	perturb = 0.0f;
	if ( trap_Argc() > 4 ) {
		trap_Argv( 4, arg, sizeof( arg ) );
		perturb = atof( arg );
	}
	scene.seed = 20261001u;
	scene.chain = 2166136261u;

	BG_PhysWorldDefInit( &def, 800.0f );
	def.workerCount = scene.workers;
	scene.world = trap_Phys_WorldCreate( &def );
	if ( !scene.world ) {
		G_Printf( "physscene: no world\n" );
		return;
	}
	trap_Phys_WorldAddBSP( scene.world, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, PHYS_BSP_PATCHES | PHYS_BSP_TRISOUPS, NULL );
	{
		/* a height field mound in one corner */
		static float heights[16 * 16];
		oaxPhysHeightField_t hf;
		int x, y;
		for ( y = 0; y < 16; y++ ) {
			for ( x = 0; x < 16; x++ ) {
				float dx = x - 7.5f, dy = y - 7.5f;
				heights[y * 16 + x] = 64.0f - ( dx * dx + dy * dy ) * 0.9f;
				if ( heights[y * 16 + x] < 0.0f ) heights[y * 16 + x] = 0.0f;
			}
		}
		memset( &hf, 0, sizeof( hf ) );
		VectorSet( hf.origin, 400.0f, 300.0f, 1.0f );
		hf.cellSize[0] = hf.cellSize[1] = 24.0f;
		hf.countX = hf.countY = 16;
		hf.minHeight = 0.0f;
		hf.maxHeight = 64.0f;
		trap_Phys_WorldAddHeightField( scene.world, &hf, heights, NULL );
	}
	Scene_Stacks();
	Scene_Debris( 320, perturb );
	Scene_Ragdolls( 16 );
	Scene_Contraptions();
	scene.active = qtrue;
	BG_OAXDebugSetInt( va( "phys_scene_%s_done", scene.tag ), 0 );
	G_Printf( "physscene %s: %d workers, %d ticks\n", scene.tag, scene.workers, scene.ticks );
}

void G_OAXPhysFrame( void ) {
	Scene_Frame();
	if ( g_physWorld ) {
		trap_Phys_WorldStep( g_physWorld, level.time - g_physLastTime );
		g_physLastTime = level.time;
	}
}

void G_OAXPhysShutdown( void ) {
	/* the engine drops the worlds with the VM; just forget the handles */
	g_physWorld = 0;
	memset( &scene, 0, sizeof( scene ) );
}
