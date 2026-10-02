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
