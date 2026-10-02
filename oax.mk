# oax.mk: objects the oax engine extensions add to the game and cgame
# modules (included by Makefile; one line per file, so features merge
# cleanly). OAX_GSRC / OAX_CGSRC name the files; the lists below are built
# from them for the base game and the missionpack build.

OAX_GSRC = \
  game/bg_oax \
  game/g_oax \
  game/g_oax_gui \
  game/g_oax_guiscript \
  game/g_oax_stats \
  game/bg_oax_traj \
  game/bg_oax_spline \
  game/g_oax_mover \
  game/g_oax_script \
  game/g_oax_events \
  game/g_oax_mover_events \
  game/g_oax_triggers \
  game/g_oax_portal \
  game/bg_oax_zone \
  game/g_oax_zone \
  game/g_oax_warp \
  game/g_oax_ulight \
  game/g_oax_skyportal \
  game/g_oax_lightstyle

OAX_CGSRC = \
  cgame/bg_oax \
  cgame/cg_oax \
  cgame/cg_oax_gui \
  cgame/bg_oax_traj \
  cgame/bg_oax_spline \
  cgame/bg_oax_zone \
  cgame/cg_oax_zone \
  cgame/cg_oax_ulight \
  cgame/cg_oax_render

OAX_GOBJ = $(OAX_GSRC:%=$(B)/$(BASEGAME)/%.o)
OAX_CGOBJ = $(OAX_CGSRC:%=$(B)/$(BASEGAME)/%.o)
OAX_MPGOBJ = $(OAX_GSRC:%=$(B)/$(MISSIONPACK)/%.o)
OAX_MPCGOBJ = $(OAX_CGSRC:%=$(B)/$(MISSIONPACK)/%.o)

# Script data the game module ships with its QVMs (script/*.script: the
# scriptEvent declarations of the events g_oax_events.c registers, and the
# default includes), copied next to vm/ so test packs and carts find them.
OAX_SCRIPT_FILES = $(wildcard script/*.script)
OAX_SCRIPT_OUT = $(OAX_SCRIPT_FILES:%=$(B)/$(BASEGAME)/%)

ifneq ($(BUILD_GAME_QVM),0)
  ifneq ($(BUILD_BASEGAME),0)
    TARGETS += $(OAX_SCRIPT_OUT)
  endif
endif

$(B)/$(BASEGAME)/script/%.script: script/%.script
	@mkdir -p $(dir $@)
	$(echo_cmd) "CP $@"
	$(Q)cp $< $@
