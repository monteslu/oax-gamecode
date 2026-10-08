// oax game code
// Copyright (C) 2026 Luis Montes
//
// This file is part of the oax game code, a fork of OpenArena's gamecode.
// It is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version. The combined game code is distributed under GPLv3.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

// oax_fx.shader: shaders of the oax cgame's effects (code/cgame/cg_oax_fx.c,
// particles/oax_weapons.prt). The *oax images are built into the engine
// (renderergl2 tr_oax_fx.c), so the effects need no art files.

oaxfx/spark
{
	nopicmip
	cull none
	{
		map *oaxspark
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

oaxfx/glow
{
	nopicmip
	cull none
	{
		map *oaxglow
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

oaxfx/smoke
{
	nopicmip
	cull none
	{
		map *oaxsoft
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

oaxfx/trailSmoke
{
	nopicmip
	cull none
	{
		map *oaxribbon
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

oaxfx/trailGlow
{
	nopicmip
	cull none
	{
		map *oaxribbon
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// water ripples (particles/oax_ground.prt)
oaxfx/ripple
{
	nopicmip
	cull none
	{
		map *oaxring
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// tyre tracks (projected decals, cg_oax_fx.c CG_OAXTrack)
oaxfx/tread
{
	nopicmip
	polygonOffset
	{
		map *oaxtread
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// hurt and low-health feedback (cg_oax_fx.c CG_OAXDamageFx): the colour and
// alpha come from the cgame, the edge falloff from the image
oaxfx/vignette
{
	nopicmip
	nomipmaps
	cull none
	{
		map *oaxvignette
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
