# Nintendo 3DS build

Goemon 64: Recompiled (Mystical Ninja Starring Goemon) for the New Nintendo
3DS and New 2DS XL, with stereoscopic 3D. The renderer and platform layer
are [rt64-3ds](https://github.com/oneup03/rt64-3ds) (`lib/rt64-3ds`).

Status: under construction (first boot). In the Azahar emulator the game
converts the ROM on its first start and plays the intro and the title
screen with sound and stereo 3D, well below full speed. It has not run on
a console yet.

## Installing

Extract the `.3dsx` to `sdmc:/3ds/Goemon64/Goemon64.3dsx` and start it from
the Homebrew Launcher, or install the `.cia` (with FBI, for instance) to
start it from the HOME Menu as "Goemon 64: Recompiled". Both use the same
folder on the SD card.

It needs your own ROM: the US version of Mystical Ninja Starring Goemon in
.z64 format (the 16 MB file the desktop version asks for), at
`sdmc:/3ds/Goemon64/mnsg.us.z64`. The game checks the ROM's hash at start and
deletes a file that does not match, so copy it rather than move it.

What the folder holds once the game has run:

```
sdmc:/3ds/Goemon64/
  Goemon64.3dsx      the program (not needed with the CIA installed)
  mnsg.us.z64        your ROM
  saves/             the saves (the game's Controller Pak)
  settings.ini       the settings (once one is changed)
  log.txt            the last run's log, useful in a bug report
```

## Controls

| 3DS | N64 |
|---|---|
| A / B | A / B |
| L | Z |
| R | R |
| START | START |
| X, D-pad up | C-Up |
| Y, D-pad down | C-Down |
| ZL, D-pad left | C-Left |
| ZR, D-pad right | C-Right |
| Circle Pad | analog stick |
| C-Stick | the camera: R + C buttons, repeated while held (or the plain C buttons: Controls settings) |

The game never reads the N64's L or D-pad. R with the C buttons moves the
camera (C-Left/Right turn it, C-Up/Down zoom). The C-Stick does that by
itself: it holds R and keeps pressing its way's C button while it is held,
so the camera keeps turning; it eases back behind Goemon once you let go.

SELECT opens the settings on the bottom screen: 3D depth and convergence,
the sticks' deadzones and what the C-Stick does, music and sound volume.

The Game page's Frame rate row picks how many frames are drawn: "60
interp." (the default: while the game runs at 30, an interpolated frame
between each two of its frames, like the desktop version's high frame rate;
it falls back to 30 where a scene is too heavy to draw twice in time),
"Game" (as many as the game draws: 30 a second in play and menus, 60 in
many cutscenes) or "30 fps" (at most 30). The stats line in `log.txt`
says how many frames were shown and why lists went without an in-between.

## Building

Needs devkitARM with libctru, citro3d and picasso; makerom and bannertool
for the CIA; clang with the MIPS target and ld.lld for the patches; a host
C++20 compiler, CMake and Ninja; the submodules
(`git submodule update --init --recursive`); and, as for the desktop build,
the decompressed US ROM `mnsg.us.decompressed.z64` at the repository root
(the recompiler's input; the game itself runs from the retail ROM).

```sh
make -C platform/3ds nmr-patch host-tools   # once: runtime patch series, recompiler
make -C platform/3ds codegen                # recompile the ROM and the patches
make -C platform/3ds cia                    # build-3ds/Goemon64Recompiled3DS.3dsx and .cia
```

`RT64_3DS=<path>` uses an rt64-3ds checkout other than `lib/rt64-3ds`.
`make -C platform/3ds` without a target builds only the .3dsx;
`make configure CMAKE_ARGS=-DGOEMON_NULL_RENDERER=ON` and a build give one
that draws nothing (bring-up). The header of `platform/3ds/Makefile` lists
the other targets. `make codegen` rebuilds the patches with `GOEMON_3DS`
into `patches/build/` and `RecompiledPatches/`, which the desktop build
shares; it regenerates its own when it is next built. The `build-3ds` job in
`.github/workflows/release.yml` runs the same steps in devkitPro's container.

`tests/` holds input scripts for unattended runs in Azahar (rt64-3ds's
`tools/azahar-run.sh --script`): `boot.txt` (the first start, from the ROM
conversion; null renderer), `title.txt` and `newgame.txt`.

The CIA's banner is a 3D model of Goemon's golden kiseru
(`cia/banner.cgfx`) with "Ganbare! Sou ganbare!" from the opening song
(`cia/banner.wav`). Both come from 3ds-banner-tools' `examples/goemon`.
