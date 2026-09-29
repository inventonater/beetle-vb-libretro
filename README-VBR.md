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

## Keeping it current

The workbench pins an exact commit of this branch in its Virtual Boy
toolchain lock, with the upstream base recorded beside it. To take a newer
upstream revision: rebase `vbr/v1` onto it, rerun parity on every available
ROM against the new upstream build, rerun the estimate checks, and move the
pin in the workbench (which reruns its own parity lanes). Keep the additions
read-only; a change that alters emulation belongs upstream.

Licence: GPL-2.0, as upstream. The workbench uses the core only in host
tools (runners, play, probes), never in a ROM.
