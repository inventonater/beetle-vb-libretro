/* libretro-vbr.h: read-only VIP research instrumentation (vbr), API 1.
 *
 * Added by the inventonater/beetle-vb-libretro fork for the n64 workbench's
 * Virtual Boy projects. Nothing behind this API writes emulator state: frames,
 * audio, timing, save states and determinism are those of the upstream base.
 *
 * A frontend that links the core may call these functions after
 * retro_load_game(). All of them are safe to call between retro_run() calls.
 *
 *   vbr_api_version()      the API version below.
 *   vbr_vip_read16(a)      one halfword of VIP address space (0x00000-0x3FFFF:
 *                          framebuffers, characters, BG maps, parameter tables,
 *                          world attributes, OAM; 0x5F800-0x5F87F: registers
 *                          as the core reports them). Same as the research
 *                          core's research_vip_read16, which is kept as an alias.
 *   vbr_vip_snapshot(o,n)  VIP 0x00000-0x3FFFF as 0x20000 halfwords, then the
 *                          0x40 register halfwords 0x5F800-0x5F87F. Returns the
 *                          halfwords written (at most n). VBR_SNAPSHOT_WORDS
 *                          fills everything.
 *   vbr_get_frame_stats()  the last completed game frame's census and estimated
 *                          VIP draw time (see struct vbr_frame_stats).
 *
 * The draw-time estimate implements the hardware-measured model published in
 * "VIP timing information" (https://skyfloogle.github.io/vip-timing/), applied
 * strip by strip to the world table the core draws. It is an estimate of a
 * model: the source says a few of its numbers are likely incorrect and that a
 * roughly 20-cycle variance was observed. Real hardware is the authority. */
#ifndef LIBRETRO_VBR_H
#define LIBRETRO_VBR_H

#include <stddef.h>
#include <stdint.h>

#define VBR_API_VERSION 1u
#define VBR_SNAPSHOT_WORDS (0x20000u + 0x40u)
#define VBR_DISPLAY_FRAME_CYCLES 400000u /* 20 MHz for one 50 Hz display frame */

struct vbr_frame_stats
{
   uint32_t size;              /* bytes the core filled (<= the caller's size) */
   uint32_t api_version;       /* VBR_API_VERSION */
   uint32_t game_frames;       /* game frames fully drawn since power-on */

   /* Estimated VIP draw time of the last fully drawn game frame. */
   uint32_t est_cycles;
   uint32_t frmcyc;            /* FRMCYC when that frame started drawing */
   uint32_t budget_cycles;     /* VBR_DISPLAY_FRAME_CYCLES * (frmcyc + 1) */

   /* The world table as drawn in the frame's first strip. */
   uint32_t worlds_normal;
   uint32_t worlds_hbias;
   uint32_t worlds_affine;
   uint32_t worlds_obj;
   uint32_t worlds_dummy;      /* neither LON nor RON set */
   uint32_t affine_pixels;     /* sum over affine worlds of width x on-screen rows */
   uint32_t objects;           /* OAM entries in the OBJ groups drawn */
   uint32_t objects_on_screen; /* of those, entries with a row inside 0-223 */

   /* CPU bus writes into VIP memory during the last complete game frame
    * (from one GAMESTART to the next), by region. */
   uint32_t chr_writes;        /* character tables */
   uint32_t bgmap_writes;      /* BG maps, parameter tables, column table */
   uint32_t world_writes;      /* world attributes */
   uint32_t oam_writes;        /* OBJ attributes */
   uint32_t fb_writes;         /* framebuffers */
   uint32_t reg_writes;        /* VIP registers */
};

#ifdef __cplusplus
extern "C" {
#endif

unsigned vbr_api_version(void);
uint16_t vbr_vip_read16(uint32_t address);
size_t vbr_vip_snapshot(uint16_t *out, size_t words);
size_t vbr_get_frame_stats(struct vbr_frame_stats *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
