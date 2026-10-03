# oax game code

This branch (`oax`) is the game code for the oax engine
([github.com/monteslu/oax-engine](https://github.com/monteslu/oax-engine)), a
fork of [OpenArena's gamecode](https://github.com/OpenArena/gamecode). It
builds the OpenArena game modules (qagame, cgame, ui) as QVMs with the oax
features: navmesh bots, vehicles, the translocator, physics effects,
particles, decals and trails, zones, sky portals, movers, triggers, in-world
GUIs and map scripts. On an engine without a feature the game falls back to
stock OpenArena behaviour.

A note on the name: upstream OpenArena calls its mod form OpenArena eXpanded
(OAX) and sets `BASEGAME=oax`, which is why the build output lands in an
`oax/` folder. The oax in this branch is the engine project above.

## Building the QVMs

You need GNU make and a C compiler (gcc or clang). On Linux:

    git clone -b oax https://github.com/monteslu/oax-gamecode.git
    cd oax-gamecode
    make

On macOS (Apple Silicon or Intel), install the Xcode command line tools
(`xcode-select --install`) and run the same commands.

The QVMs land in `build/release-<os>-<arch>/oax/vm/` (`cgame.qvm`,
`qagame.qvm`, `ui.qvm`), for example `build/release-linux-x86_64/oax/vm/` or
`build/release-darwin-arm64/oax/vm/`. The script, particle, shader and model
files the game modules use are copied next to them. (On Linux, gcc and
clang build byte-identical QVMs.)

    make pk3

builds the same and packs it into
`build/release-<os>-<arch>/oax/zzz-oax-game.pk3` (`vm/`, `script/`,
`models/`, `particles/`, `scripts/`).

A plain `make` builds only the base game QVMs. `make BUILD_GAME_SO=1` also
builds native `.so`/`.dylib` modules, and `make BUILD_MISSIONPACK=1` the
missionpack build.

## Installing

You need OpenArena 0.8.8's data (`sudo apt install openarena-data` on
Debian/Ubuntu puts it in `/usr/share/games/openarena/baseoa`; elsewhere,
unpack OpenArena 0.8.8 from openarena.ws) and the oax engine. Put the pk3 in
the `baseoa` folder of a home folder of your choice and start the engine
with OpenArena's data as the base path:

    mkdir -p ~/oax-home/baseoa
    cp build/release-*/oax/zzz-oax-game.pk3 ~/oax-home/baseoa/

    ioquake3 +set fs_basepath /usr/share/games/openarena \
      +set com_basegame baseoa +set fs_homepath ~/oax-home \
      +set sv_pure 0 +set vm_game 1 +set vm_cgame 1 +set vm_ui 1

`fs_basepath` is the folder that contains `baseoa`. `sv_pure 0` lets the pk3
in the home folder load, the `vm_*` settings run the QVMs, and the `zzz`
name makes the pk3 load after OpenArena's own pk3s, so its QVMs replace
OpenArena's. To check, the console's `debugvalues` command lists `g_oax` and
`cg_oax` (the oax game code's version) once a map is running. The oax
engine's docs/getting-started.md has the full walkthrough.

Game cvars and test commands: [docs/cvars.md](docs/cvars.md).

## Licence

The OpenArena gamecode is GPLv2 or later ([LICENSE](LICENSE)), and so are
the new oax files (three that follow id Tech 4 code are GPLv3 or later).
Six files carry code from id Software's DOOM-3 GPL
release, which is GPLv3, so the combined game code is distributed under the
GNU General Public License version 3 ([COPYING-GPLv3.txt](COPYING-GPLv3.txt)),
with the DOOM-3 release's additional terms for the files that carry them
([DOOM3-ADDITIONAL-TERMS.txt](DOOM3-ADDITIONAL-TERMS.txt); the files are
listed in [docs/idtech4-attribution.md](docs/idtech4-attribution.md)). The
`iqmguy` player model's terms are in
[models/players/iqmguy/LICENSE.txt](models/players/iqmguy/LICENSE.txt).

The rest of this file is the upstream OpenArena README, kept as it is.

---

# OpenArena gamecode
![Build status](https://github.com/openarena/gamecode/actions/workflows/main.yml/badge.svg) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/90453976351f455f89d42651658fa63a)](https://www.codacy.com/app/github_43/gamecode_2?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=OpenArena/gamecode&amp;utm_campaign=Badge_Grade)

## Description ##
This is the game code part of OpenArena. In mod form it is referred as OpenArenaExpanded (OAX).

## Building ##

You need a C-compiler (tested with gcc and clang) and GNU make then just type
```
make
```
and the qvm-files will be build. Ready to be packed into a pk3-file.

See https://github.com/OpenArena/gamecode/wiki/Build-instruction for more details.

See http://openarena.wikia.com/wiki/OpenArena_eXpanded for alternative build options

## Extracting entities ##
It is possible to extract entity definition for use with GtkRadiant and NetRadiant like this:

```
cd code/game
./extract_entities.sh > openarena.def
```

## Links ##
Development documentation is located here: https://github.com/OpenArena/gamecode/wiki

The development board on the OpenArena forum: http://openarena.ws/board/index.php?board=30.0

In particular the Open Arena Expanded topic: http://openarena.ws/board/index.php?topic=1908.0

## License ##

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
