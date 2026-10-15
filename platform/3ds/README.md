# Nintendo 3DS build

Goemon 64: Recompiled (Mystical Ninja Starring Goemon) for the Nintendo 3DS
family, with stereoscopic 3D. A New 3DS or New 2DS XL is recommended; an Old
3DS or 2DS runs it at a lower frame rate (see Performance). The renderer and
platform layer are [rt64-3ds](https://github.com/oneup03/rt64-3ds)
(`lib/rt64-3ds`).

## Installing

- **CIA** (`Goemon64Recompiled3D-3ds-<commit>.cia`): install it with FBI or
  another CIA installer; the rolling release's notes show a QR code for
  FBI's Remote Install. It appears on the HOME Menu as "Goemon 64:
  Recompiled 3D". On an Old 3DS or 2DS, use the CIA (see Performance).
- **.3dsx** (`Goemon64Recompiled3D-3ds-<commit>.zip`): extract it to the root
  of the SD card and start `3ds/Goemon64/Goemon64.3dsx` from the Homebrew
  Launcher.

Both use the same folder and need your own ROM: the US version of Mystical
Ninja Starring Goemon in .z64 format (the 16 MB file the desktop version
asks for), at `sdmc:/3ds/Goemon64/mnsg.us.z64`. The game checks the ROM's
hash at start and deletes a file that does not match, so copy it rather
than move it.

For sound, the console's DSP firmware must be on the SD card at
`sdmc:/3ds/dspfirm.cdc`, as for any homebrew with audio. Dump it once with
Luma3DS: open the Rosalina menu (L + D-pad down + SELECT), then
Miscellaneous options > Dump DSP firmware. Without it the game runs silent.

On the SD card, once installed:

```
sdmc:/
  3ds/
    dspfirm.cdc          the DSP firmware, for sound
    Goemon64/
      Goemon64.3dsx      the program (not needed with the CIA installed)
      mnsg.us.z64        your ROM
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

SELECT opens the settings on the bottom screen: the 3D page (see Stereo
3D), the sticks' deadzones and what the C-Stick does, and the Game page's
frame rate, sky, music and sound volume.

## Stereo 3D

The console's 3D slider turns the 3D on and sets how strong it is; with
the slider down the game draws one image. The settings' 3D page tunes it:

| Setting | What it does |
|---|---|
| 3D depth | The separation between the eyes at the top of the slider (default 50). Raise it for more depth, lower it if the image is hard to focus. |
| Convergence | How far into the scene the screen's plane sits (default 2.0): things nearer than it come out of the screen, things beyond it go in. |
| HUD depth | Where the HUD, menus and text sit (50, the default, is on the screen's plane; higher brings them out of the screen, lower pushes them in). |
| Auto convergence | On by default: when something comes close to the camera (a wall, a character in a close-up, the zoomed camera), the screen's plane moves nearer so it doesn't pop out too far. Convergence stays the farthest it goes. |
| Comfort target | How far out of the screen Auto convergence lets near things come (0 by default; higher for more pop-out, lower to keep everything behind the screen). |
| Ghost reduction: contrast, Ghost black floor | Ease ghosting (a faint double image of high-contrast edges on some screens) by softening contrast and lifting blacks. Off by default (100% and 0%). |

The Game page's Sky row picks the sky: 3D (a sky that wraps around the
scene and sits at its depth, the default on a New 3DS) or Flat (the game's
own scrolling backdrop, the default on an Old 3DS, where it costs far less).
The game's painted backdrops stay at the far distance, and menus and text
sit at the HUD depth.

## Performance

**New 3DS / New 2DS XL:** 60 fps most of the time. The game itself runs at
30, and by default an interpolated frame is drawn between each two of its
frames (the Game page's Frame rate: 60 interp., like the desktop version's
high frame rate); the heaviest areas drop back toward 50, and a scene too
heavy to draw twice in time falls back to 30 for that frame. Set Frame rate
to Game for the original rate (30 in play and menus, 60 in many cutscenes),
or 30 fps for at most 30.

**Old 3DS / 2DS:** runs at about 20-30 fps where the game asks 30, lower in
the heaviest scenes. It starts with Frame rate at 30 fps, as it can't afford
the frames in between, and the Sky at Flat. Install the CIA rather than
running the .3dsx: an Old 3DS gives a game 64 MB, and the CIA leaves enough
to hold the 16 MB ROM in memory, where the Homebrew Launcher may not; the
game then reads the ROM from the SD card as it needs it, with slower loads.

## AI use

The 3DS port, including this build's code and documentation and the
rt64-3ds renderer it uses, was written with the help of AI tools (Claude),
under the developer's direction and with their testing on real consoles.

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
