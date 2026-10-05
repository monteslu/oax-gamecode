# oax game code
# Copyright (C) 2026 Luis Montes
#
# This file is part of the oax game code, a fork of OpenArena's gamecode.
# It is free software; you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version. The combined game code is distributed under GPLv3.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

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
  game/g_oax_ctfstats \
  game/g_oax_terrain \
  game/g_oax_navbot \
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
  game/g_oax_lightstyle \
  game/bg_oax_phys \
  game/g_oax_phys \
  game/g_oax_fx \
  game/bg_oax_vehicle \
  game/g_oax_vehicle \
  game/g_oax_assault \
  game/g_oax_vehbot \
  game/g_oax_navlinks \
  game/g_oax_teleport \
  game/g_oax_translocator \
  game/g_oax_place

OAX_CGSRC = \
  cgame/bg_oax \
  cgame/cg_oax \
  cgame/cg_oax_gui \
  cgame/bg_oax_traj \
  cgame/bg_oax_spline \
  cgame/bg_oax_zone \
  cgame/cg_oax_zone \
  cgame/cg_oax_ulight \
  cgame/cg_oax_render \
  cgame/bg_oax_vehicle \
  cgame/cg_oax_vehicle \
  cgame/cg_oax_assault \
  cgame/bg_oax_phys \
  cgame/cg_oax_phys \
  cgame/cg_oax_skel \
  cgame/cg_oax_fx

OAX_GOBJ = $(OAX_GSRC:%=$(B)/$(BASEGAME)/%.o)
OAX_CGOBJ = $(OAX_CGSRC:%=$(B)/$(BASEGAME)/%.o)
OAX_MPGOBJ = $(OAX_GSRC:%=$(B)/$(MISSIONPACK)/%.o)
OAX_MPCGOBJ = $(OAX_CGSRC:%=$(B)/$(MISSIONPACK)/%.o)

# Script data the game module ships with its QVMs (script/*.script: the
# scriptEvent declarations of the events g_oax_events.c registers, and the
# default includes), copied next to vm/ so test packs and carts find them.
OAX_SCRIPT_FILES = $(wildcard script/*.script)
OAX_SCRIPT_OUT = $(OAX_SCRIPT_FILES:%=$(B)/$(BASEGAME)/%)

# Effect data the cgame uses: particle decls (particles/*.prt)
# and their shaders (scripts/*.shader), copied the same way.
OAX_FX_FILES = $(wildcard particles/*.prt) $(wildcard scripts/*.shader)
OAX_FX_OUT = $(OAX_FX_FILES:%=$(B)/$(BASEGAME)/%)

ifneq ($(BUILD_GAME_QVM),0)
  ifneq ($(BUILD_BASEGAME),0)
    TARGETS += $(OAX_SCRIPT_OUT) $(OAX_FX_OUT)
  endif
endif

$(B)/$(BASEGAME)/particles/%.prt: particles/%.prt
	@mkdir -p $(dir $@)
	$(echo_cmd) "CP $@"
	$(Q)cp $< $@

$(B)/$(BASEGAME)/scripts/%.shader: scripts/%.shader
	@mkdir -p $(dir $@)
	$(echo_cmd) "CP $@"
	$(Q)cp $< $@

$(B)/$(BASEGAME)/script/%.script: script/%.script
	@mkdir -p $(dir $@)
	$(echo_cmd) "CP $@"
	$(Q)cp $< $@

# Player models the oax cgame brings (models/players/*: the skeletal IQM
# player and its license), copied next to vm/ like the scripts.
OAX_MODEL_FILES = $(wildcard models/players/*/*)
OAX_MODEL_OUT = $(OAX_MODEL_FILES:%=$(B)/$(BASEGAME)/%)

ifneq ($(BUILD_GAME_QVM),0)
  ifneq ($(BUILD_BASEGAME),0)
    TARGETS += $(OAX_MODEL_OUT)
  endif
endif

$(B)/$(BASEGAME)/models/%: models/%
	@mkdir -p $(dir $@)
	$(echo_cmd) "CP $@"
	$(Q)cp $< $@

# `make pk3`: the release build, then the files a tester installs, zipped
# into $(BR)/$(BASEGAME)/zzz-oax-game.pk3 (vm/ plus the script, model,
# particle and shader data above). Copy it to <homepath>/baseoa/; the zzz
# name sorts it after the stock pk3s, so its QVMs win.
OAX_PK3_NAME = zzz-oax-game.pk3
OAX_PK3_DIRS = vm script models particles scripts

.PHONY: pk3
pk3: release
	@echo "PK3 $(BR)/$(BASEGAME)/$(OAX_PK3_NAME)"
	@cd $(BR)/$(BASEGAME) && rm -f $(OAX_PK3_NAME) && \
	  zip -q -r -X $(OAX_PK3_NAME) $$(for d in $(OAX_PK3_DIRS); do \
	    if [ -d $$d ]; then echo $$d; fi; done)
