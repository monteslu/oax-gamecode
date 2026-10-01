# oax.mk: objects the oax engine extensions add to the game and cgame
# modules (included by Makefile; one line per file, so features merge
# cleanly). OAX_GSRC / OAX_CGSRC name the files; the lists below are built
# from them for the base game and the missionpack build.

OAX_GSRC = \
  game/bg_oax \
  game/g_oax \
  game/g_oax_stats

OAX_CGSRC = \
  cgame/bg_oax \
  cgame/cg_oax

OAX_GOBJ = $(OAX_GSRC:%=$(B)/$(BASEGAME)/%.o)
OAX_CGOBJ = $(OAX_CGSRC:%=$(B)/$(BASEGAME)/%.o)
OAX_MPGOBJ = $(OAX_GSRC:%=$(B)/$(MISSIONPACK)/%.o)
OAX_MPCGOBJ = $(OAX_CGSRC:%=$(B)/$(MISSIONPACK)/%.o)
