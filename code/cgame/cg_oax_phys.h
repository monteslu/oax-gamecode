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
cg_oax_phys.h: cosmetic physics (cg_oax_phys.c) and skeletal players with
ragdolls (cg_oax_skel.c) in the cgame.
===========================================================================
*/
#ifndef CG_OAX_PHYS_H
#define CG_OAX_PHYS_H

typedef struct {
	int			world;			/* the cosmetic Box3D world, 0 = none */
	int			lastTime;
	int			fragments;		/* local entities with a body */
	int			ragdolls;
	qhandle_t	debrisShader;
} cgPhys_t;

extern cgPhys_t	cgPhys;
extern vmCvar_t	cg_physics;

/* cg_oax_phys.c */
void		CG_PhysInit( void );
void		CG_PhysFrame( void );
qboolean	CG_PhysActive( void );
void		CG_PhysLaunchFragment( localEntity_t *le );
void		CG_PhysFreeLocalEntity( localEntity_t *le );
void		CG_PhysAddFragment( localEntity_t *le );
void		CG_PhysExplosion( int weapon, const vec3_t origin, const vec3_t dir, qboolean wall );
void		CG_PhysTest_f( void );

/* cg_oax_skel.c */
void		CG_SkelInit( void );
void		CG_SkelFrame( void );
qboolean	CG_SkelRegisterClient( clientInfo_t *ci, const char *modelName );
qboolean	CG_SkelPlayer( centity_t *cent, clientInfo_t *ci, refEntity_t *legs, refEntity_t *torso, int renderfx, float shadowPlane );
void		CG_SkelGibbed( centity_t *cent );

/* cg_localents.c / cg_localents.c helpers the physics fragments reuse */
extern localEntity_t	cg_localEntities[];
#define CG_OAX_MAX_LOCAL_ENTITIES 512	/* MAX_LOCAL_ENTITIES in cg_localents.c */
void		CG_BloodTrail( localEntity_t *le );
void		CG_FreeLocalEntity( localEntity_t *le );
void		CG_FragmentBounceMark( localEntity_t *le, trace_t *trace );
void		CG_FragmentBounceSound( localEntity_t *le, trace_t *trace );

#endif
