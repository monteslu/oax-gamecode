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
bg_oax_assault.h: what the game and the cgame share about Assault
(g_gametype GT_ASSAULT; g_oax_assault.c, cg_oax_assault.c).

The round state is the info string CS_OAX_ASSAULT:
  r  round, 1 or 2 (the teams swap roles for round 2)
  a  the attacking team (TEAM_RED or TEAM_BLUE)
  p  phase (OAX_AS_*)
  s  level time the round's clock started
  l  the round's time limit, ms (round 2: the time to beat when round 1
     was won)
  t  round 1's result: its time in ms, -1 if the attackers failed, 0 not
     played yet
  e  level time the round ended, 0 while it runs
  w  the round's outcome: 1 the attackers made it, 2 they ran out of time
  m  the briefing (info_oax_assault "message")

Each objective is the info string CS_OAX_ASSAULTOBJ + i:
  t  type (OAX_ASO_*), o order, s state (OAX_ASOS_*), h health percent,
  p use progress percent, f final, x y z its centre, n its name
===========================================================================
*/
#ifndef BG_OAX_ASSAULT_H
#define BG_OAX_ASSAULT_H

#define OAX_AS_MAX_OBJECTIVES	8

/* phases */
#define OAX_AS_PRE		0	/* players are in; the clock starts shortly */
#define OAX_AS_LIVE		1
#define OAX_AS_OVER		2	/* the round ended; the next one or the end follows */

/* objective types */
#define OAX_ASO_DESTROY	0	/* the attackers shoot it to pieces */
#define OAX_ASO_REACH	1	/* an attacker gets there */
#define OAX_ASO_USE		2	/* an attacker holds use beside it */
#define OAX_ASO_TRIGGER	3	/* completed when something targets it */

/* objective states */
#define OAX_ASOS_LOCKED	0	/* an earlier objective is still up */
#define OAX_ASOS_ACTIVE	1
#define OAX_ASOS_DONE	2

#endif
