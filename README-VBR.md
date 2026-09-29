# vbr: read-only VIP research instrumentation

This fork of [libretro/beetle-vb-libretro](https://github.com/libretro/beetle-vb-libretro)
adds a small, read-only view into the Virtual Boy's video processor (VIP) for
the [n64 workbench](https://github.com/inventonater/n64)'s Virtual Boy
projects. Branch `vbr/v1` sits on upstream `83ed426` ("fix visual issue in
Dragon Hopper"), the revision the workbench pinned before this fork.

It is not a new frontend, and it changes no emulation. Frames, audio, timing,
save states and determinism are the upstream core's; the only additions are
functions that read VIP state and counters that count VIP writes.

## What it adds

The public header is [`libretro-vbr.h`](libretro-vbr.h) (API version 1):

| Function | Returns |
| --- | --- |
| `vbr_api_version()` | `VBR_API_VERSION` |
| `vbr_vip_read16(address)` | One halfword of VIP address space: framebuffers, characters, BG maps, parameter tables, world attributes and OAM (`0x00000`-`0x3FFFF`), or a register (`0x5F800`-`0x5F87F`) as the core reports it. `research_vip_read16` is an alias for the Game Reference Library's earlier research core. |
| `vbr_vip_snapshot(out, words)` | The whole VIP memory plus the register block, `VBR_SNAPSHOT_WORDS` halfwords. |
| `vbr_get_frame_stats(out, size)` | The last fully drawn game frame: world census by mode, affine pixels, OBJ entries (and those on screen), CPU writes into each VIP region during the frame before, `FRMCYC`, and an **estimated VIP draw time** against the budget for that `FRMCYC`. |

The implementation is `mednafen/vb/vip_research.inc`, included by `vip.c`.
Its hooks are one call each at: power-on (reset the counters), each VIP bus
write (count it), GAMESTART (close the write counts, open an estimate), the
start of each 8-row strip (add that strip's modelled cost), the last strip
(publish), and an XP reset (drop the unfinished estimate).

## The draw-time estimate

The core draws every frame instantly, so a scene that would overrun on a real
Virtual Boy looks fine in emulation. The estimate applies the hardware-measured
model in [VIP timing information](https://skyfloogle.github.io/vip-timing/)
strip by strip to the world table the core is about to draw:

- 54,688 cycles per frame; 308 for an END world; 561 per dummy world;
- normal and H-bias worlds 880, affine 908, OBJ worlds 757 (plus 28,896 for
  every fourth OBJ world from the fifth);
- affine: 80 per on-screen row plus 4 per pixel of the full window width;
- H-bias: 98 per row plus 4 per tile the row touches;
- normal: 91 per tile row within a strip, 2 per tile, 2 per tile per pixel row;
- objects: 42 per strip they appear on (plus 5 if their top row is not in it,
  plus 2 per visible row), 1 per strip they do not;
- the published per-strip base costs and edge adjustments.

It is an estimate of a model. The source reports about 20 cycles of variance
and says a few of its numbers are likely incorrect; world-level terms are
taken from each frame's first strip. Real hardware is the authority.

**Checks against the source's own observations** (Beetle `83ed426` plus this
branch, the Game Reference Library's retail images):

| Scene | The study says | This estimate |
| --- | --- | --- |
| Blank image (world 31 is END) | 54,996 cycles measured | 54,996 |
| Mario's Tennis, singles rally | "well within" the 400k budget; FRMCYC 1 | about 307k; FRMCYC 1 |
| Mario's Tennis, doubles rally | "can easily run over 400k" | mean about 360k; peaks about 437k |
| Mario's Tennis title (court, net, one affine logo per eye) | well past one frame | about 669k |

## Proof that emulation is unchanged

`tools/vbr/parity.c` loads a core by path, runs a ROM for a fixed number of
frames with fixed inputs, and hashes every video frame, all audio and the
final system RAM without calling any vbr function. The upstream `83ed426`
build and this branch's build must print identical hashes for every ROM
checked. `tools/vbr/stats.c` records the per-frame census and estimate.

```sh
make -j8 CC=clang CXX=clang++
cc -O2 -I libretro-common/include tools/vbr/parity.c -o parity
cc -O2 -I libretro-common/include -I . tools/vbr/stats.c -o stats
./parity ./mednafen_vb_libretro.dylib game.vb 6000
./stats ./mednafen_vb_libretro.dylib game.vb 6000 [script]
```

## Results at adoption (`2f6e242`)

**Parity.** All 31 retail and prototype images in the Game Reference Library,
6,000 frames each with the library's smoke inputs: identical frame, audio and
RAM hashes from upstream `83ed426` and this branch. Four were rerun against
the exact pinned build (the first run used a build that differed only in its
embedded version string), with the same result.

**Estimates across the library**, same runs. "Over budget" counts drawn game
frames whose estimate exceeds 400,000 × (FRMCYC + 1). The fixed inputs reach
menus, attract modes and early play only; they do not reach, for example,
Jack Bros.' maze transition that the study measured at over 1.6 million
cycles. Where the study describes a scene, the note says so; the other rows
are the model's prediction, not a hardware observation.

| Image | Peak estimate (cycles) | Frames over budget | Game frames drawn | Note |
| --- | ---: | ---: | ---: | --- |
| 3-D Tetris (USA) | 167,090 | 0 | 3614 |  |
| Galactic Pinball (Japan, USA) (En) | 418,752 | 2 | 5993 |  |
| Golf (USA) | 1,022,873 | 606 | 1889 | study: course map "over a million cycles" |
| Innsmouth no Yakata (Japan) | 419,178 | 177 | 5987 | study: full-screen affine logo at FRMCYC 0 runs at half speed |
| Jack Bros. (USA) | 613,831 | 44 | 5975 |  |
| Jack Bros. no Meiro de Hiihoo! (Japan) | 613,831 | 34 | 5975 |  |
| Mario Clash (Japan, USA) (En) | 259,500 | 0 | 5958 |  |
| Mario's Tennis (Japan, USA) (En) | 668,657 | 0 | 3774 | study: title well past one frame; FRMCYC 1 in play |
| Nester's Funky Bowling (USA) | 440,849 | 0 | 4738 |  |
| Panic Bomber (USA) | 319,340 | 0 | 5979 |  |
| Red Alarm (Japan) | 127,640 | 0 | 2219 |  |
| Red Alarm (USA) | 127,640 | 0 | 2219 |  |
| SD Gundam - Dimension War (Japan) | 376,323 | 0 | 3428 |  |
| Space Invaders - Virtual Collection (Japan) | 418,279 | 55 | 5797 | study: same as Innsmouth |
| Space Squash (Japan) | 459,202 | 1303 | 5935 | one full-screen affine world at FRMCYC 0 |
| T&E Virtual Golf (Japan) | 1,022,873 | 1202 | 3723 | same game as Golf |
| Teleroboxer (Japan, USA) (En) | 511,330 | 21 | 5440 |  |
| Tobidase! Panibon (Japan) | 319,340 | 0 | 5979 |  |
| V-Tetris (Japan) (En) | 531,084 | 270 | 5984 | one full-screen affine world at FRMCYC 0 |
| Vertical Force (Japan) | 690,337 | 629 | 5992 | as the USA release |
| Vertical Force (USA) | 690,337 | 629 | 5992 | two large affine worlds at FRMCYC 0 |
| Virtual Bowling (Japan) (En) | 1,263,685 | 0 | 4292 |  |
| Virtual Boy Wario Land (Japan, USA) (En) | 349,849 | 0 | 5997 |  |
| Virtual Fishing (Japan) | 187,548 | 0 | 5998 |  |
| Virtual Lab (Japan) | 334,941 | 0 | 2997 |  |
| Virtual League Baseball (USA) | 169,736 | 0 | 2998 |  |
| Virtual Pro Yakyuu '95 (Japan) | 169,736 | 0 | 2998 |  |
| Waterworld (USA) | 158,593 | 0 | 5406 |  |
| Bound High (Japan) (En) (Proto) | 599,571 | 0 | 2997 | study: full-screen affine play area, FRMCYC 1 |
| Niko-chan Battle (Japan) (Proto) | 890,345 | 0 | 3948 |  |
| Space Pinball (Japan) (En) (Proto) | 456,205 | 6 | 5994 |  |

## Keeping it current

The workbench pins an exact commit of this branch in its Virtual Boy
toolchain lock, with the upstream base recorded beside it. To take a newer
upstream revision: rebase `vbr/v1` onto it, rerun parity on every available
ROM against the new upstream build, rerun the estimate checks, and move the
pin in the workbench (which reruns its own parity lanes). Keep the additions
read-only; a change that alters emulation belongs upstream.

Licence: GPL-2.0, as upstream. The workbench uses the core only in host
tools (runners, play, probes), never in a ROM.
