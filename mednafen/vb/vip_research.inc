/* vip_research.inc: read-only VIP research instrumentation (vbr API 1).
 *
 * Included by vip.c after its register state (inventonater fork). Every
 * function here reads VIP state or its own counters; none writes emulator
 * state, so frames, audio, timing and save states are the upstream core's.
 * See libretro-vbr.h for the frontend API.
 *
 * The draw-time estimate follows "VIP timing information"
 * (https://skyfloogle.github.io/vip-timing/): a 54,688-cycle frame base, 308
 * for an END world, 561 per dummy world, per-world bases (normal and H-bias
 * 880, affine 908, OBJ 757 plus 28,896 for every fourth OBJ world from the
 * fifth), per-row and per-tile terms, per-object per-strip terms, and the
 * published per-strip adjustments. It is applied strip by strip to the world
 * table the core is about to draw, so mid-frame changes are seen as the
 * hardware would see them. Simplifications: world-level terms are taken from
 * the first strip's table; the per-strip edge adjustments are applied as the
 * source words them, which it calls verified but hard to explain. */

#include "../../libretro-vbr.h"

enum { VBR_W_CHR, VBR_W_BGMAP, VBR_W_WORLD, VBR_W_OAM, VBR_W_FB, VBR_W_REG, VBR_W_COUNT };

static struct vbr_frame_stats VBR_Published; /* last fully drawn game frame */
static struct vbr_frame_stats VBR_Frame;     /* frame being drawn */
static uint32 VBR_Writes[VBR_W_COUNT];       /* current game frame */
static uint32 VBR_WritesLast[VBR_W_COUNT];   /* previous complete game frame */
static bool VBR_Drawing;

static void VBR_Reset(void)
{
   memset(&VBR_Published, 0, sizeof(VBR_Published));
   memset(&VBR_Frame, 0, sizeof(VBR_Frame));
   memset(VBR_Writes, 0, sizeof(VBR_Writes));
   memset(VBR_WritesLast, 0, sizeof(VBR_WritesLast));
   VBR_Drawing = false;
}

/* Classify one CPU bus write by the VIP region it lands in. Mirrors the
 * address decoding of VIP_Write8/VIP_Write16. */
static INLINE void VBR_CountWrite(uint32 A)
{
   switch(A >> 16)
   {
      case 0x0:
      case 0x1:
         VBR_Writes[((A & 0x7FFF) >= 0x6000) ? VBR_W_CHR : VBR_W_FB]++;
         break;

      case 0x2:
      case 0x3:
      {
         const uint32 o = A & 0x1FFFF;
         if(o >= 0x1E000)
            VBR_Writes[VBR_W_OAM]++;
         else if(o >= 0x1D800 && o < 0x1DC00)
            VBR_Writes[VBR_W_WORLD]++;
         else
            VBR_Writes[VBR_W_BGMAP]++;
         break;
      }

      case 0x4:
      case 0x5:
         if(A >= 0x5E000)
            VBR_Writes[VBR_W_REG]++;
         break;

      case 0x7:
         if(A >= 0x8000)
            VBR_Writes[VBR_W_CHR]++;
         break;
   }
}

/* GAMESTART: close the write counts of the game frame that just ended and,
 * when the VIP is about to draw, open a new estimate. */
static void VBR_GameStart(bool drawing)
{
   memcpy(VBR_WritesLast, VBR_Writes, sizeof(VBR_Writes));
   memset(VBR_Writes, 0, sizeof(VBR_Writes));

   if(drawing)
   {
      memset(&VBR_Frame, 0, sizeof(VBR_Frame));
      VBR_Frame.frmcyc = FRMCYC;
      VBR_Frame.est_cycles = 54688;
      VBR_Drawing = true;
   }
}

static void VBR_AbortFrame(void)
{
   VBR_Drawing = false;
}

static INLINE int32 VBR_FloorDiv8(int32 v)
{
   return (v >= 0) ? (v / 8) : -((7 - v) / 8);
}

/* Tiles a row of a normal or H-bias world touches: every tile between the
 * leftmost and rightmost source X of the two eyes, across the full width. */
static INLINE int32 VBR_TilesAcross(int32 left_x, int32 right_x, int32 width)
{
   const int32 lo = left_x < right_x ? left_x : right_x;
   const int32 hi = left_x < right_x ? right_x : left_x;

   if(width <= 0)
      return 0;
   return ((lo & 7) + (hi - lo) + width + 7) >> 3;
}

/* One background world (0 normal, 1 H-bias, 2 affine) in strip `block`, with
 * its once-per-frame terms when block is 0. */
static int32 VBR_BGWorldStrip(const uint16 *w, unsigned bgm, uint32 block)
{
   const int32 gy = (int16)sign_11_to_s16(w[3]);
   const int32 mx = (int16)w[4];
   const int32 mp = (int16)sign_9_to_s16(w[5]);
   const int32 my = (int16)w[6];
   const int32 raw_width = (int16)sign_11_to_s16(w[7]) + 1;
   const int32 width = raw_width > 0 ? raw_width : 0;
   const int32 height = (w[8] & 0x3FF) + 1;
   const uint32 param_base = w[9];
   const int32 s0 = (int32)block * 8;
   const int32 s1 = s0 + 8;
   const int32 top = gy;
   int32 bottom = gy + height;
   int32 cost = 0;
   int32 r0, r1;

   /* Normal and H-bias worlds cover the rest of GY's strip even when H is
    * shorter. */
   if(bgm != 2 && gy >= 0 && gy < 224)
   {
      const int32 strip_end = (gy / 8 + 1) * 8;
      if(bottom < strip_end)
         bottom = strip_end;
   }

   if(block == 0)
   {
      cost += (bgm == 2) ? 908 : 880;

      if(top < 0 && bottom > 0)
      {
         const int32 visible_bottom = bottom < 224 ? bottom : 224;
         if(bgm == 2)
            cost += 5;
         else
            cost += (visible_bottom <= 8) ? 6 : 4;
      }
      if(bgm == 2 && top < 224 && bottom > 224)
         cost += 3;
      if(top >= 224)
         cost += (bgm == 2) ? 2 : -1;
      if(top >= 216)
         cost -= (bgm == 2) ? 12 : 9;
   }

   r0 = top > s0 ? top : s0;
   if(r0 < 0)
      r0 = 0;
   r1 = bottom < s1 ? bottom : s1;
   if(r1 > 224)
      r1 = 224;

   if(r0 < r1)
   {
      const bool has_top = top >= s0 && top < s1;
      const bool has_bottom = (bottom - 1) >= s0 && (bottom - 1) < s1;
      int32 r;

      if(bgm == 2)
      {
         cost += 14 - (has_top ? 1 : 0);
         cost += (r1 - r0) * (80 + 4 * width);
         VBR_Frame.affine_pixels += (uint32)((r1 - r0) * width);
      }
      else if(bgm == 1)
      {
         cost += 16 - (has_top ? 4 : (has_bottom ? 3 : 0));
         for(r = r0; r < r1; r++)
         {
            const uint32 row = (uint32)(r - gy) * 2;
            const int32 hofst_l = (int16)DRAM[(param_base + row) & 0xFFFF];
            const int32 hofst_r = (int16)DRAM[((param_base + row) | 1) & 0xFFFF];
            cost += 98 + 4 * VBR_TilesAcross((int16)(mx - mp + hofst_l), (int16)(mx + mp + hofst_r), width);
         }
      }
      else
      {
         const int32 tiles = VBR_TilesAcross((int16)(mx - mp), (int16)(mx + mp), width);
         int32 previous_tile_row = 0;
         bool first = true;

         cost += 16 - (has_top ? 4 : (has_bottom ? 3 : 0));
         for(r = r0; r < r1; r++)
         {
            const int32 tile_row = VBR_FloorDiv8((int16)(my + (r - gy)));
            if(first || tile_row != previous_tile_row)
            {
               cost += 91 + 2 * tiles;
               previous_tile_row = tile_row;
               first = false;
            }
            cost += 2 * tiles;
         }
      }
   }
   else if(s1 <= top)
      cost += 5; /* a strip the world lies fully below */

   return cost;
}

/* One OBJ group in strip `block`, walking OAM exactly as DrawOBJ does. */
static int32 VBR_ObjGroupStrip(unsigned group, uint32 block)
{
   const int32 start = SPT[group] & 1023;
   const int32 end = group ? (SPT[group - 1] & 1023) : 1023;
   const uint32 s0 = block * 8;
   int32 oam = start;
   int32 cost = 0;

   do
   {
      const uint32 jy = DRAM[((0x1E000 + oam * 8) >> 1) + 2];
      unsigned visible = 0;
      bool top_row = false;
      unsigned y;

      for(y = 0; y < 8; y++)
      {
         const uint32 tile_y = ((s0 + y) - jy) & 0xFF;
         if(tile_y < 8)
         {
            visible++;
            if(tile_y == 0)
               top_row = true;
         }
      }
      cost += visible ? (42 + (top_row ? 0 : 5) + 2 * (int32)visible) : 1;

      if(block == 0)
      {
         VBR_Frame.objects++;
         for(y = 0; y < 8; y++)
         {
            if(((jy + y) & 0xFF) < 224)
            {
               VBR_Frame.objects_on_screen++;
               break;
            }
         }
      }
   } while((oam = (oam - 1) & 1023) != end);

   return cost;
}

/* Called just before the core draws strip `block` (0-27) of a game frame. */
static void VBR_AccountStrip(uint32 block)
{
   unsigned group = 3;
   unsigned obj_worlds = 0;
   int32 cost = 0;
   int world;

   if(!VBR_Drawing)
      return;

   for(world = 31; world >= 0; world--)
   {
      const uint16 *w = &DRAM[(0x1D800 + world * 0x20) >> 1];
      const unsigned bgm = (w[0] >> 12) & 3;
      const bool shown = (w[0] & 0xC000) != 0;

      if(w[0] & 0x40)
      {
         if(block == 0)
            cost += 308;
         break;
      }

      if(!shown)
      {
         if(block == 0)
         {
            cost += 561;
            VBR_Frame.worlds_dummy++;
         }
      }
      else if(bgm == 3)
      {
         if(block == 0)
         {
            cost += 757;
            if(obj_worlds >= 4 && ((obj_worlds - 4) & 3) == 0)
               cost += 28896;
            VBR_Frame.worlds_obj++;
         }
         cost += VBR_ObjGroupStrip(group, block);
      }
      else
      {
         if(block == 0)
         {
            if(bgm == 0)
               VBR_Frame.worlds_normal++;
            else if(bgm == 1)
               VBR_Frame.worlds_hbias++;
            else
               VBR_Frame.worlds_affine++;
         }
         cost += VBR_BGWorldStrip(w, bgm, block);
      }

      /* DrawOBJ's group selection: every OBJ-mode world steps down a group. */
      if(bgm == 3)
      {
         if(shown)
            obj_worlds++;
         if(group)
            group--;
      }
   }

   VBR_Frame.est_cycles = (uint32)((int32)VBR_Frame.est_cycles + cost);
}

/* Called after the core draws a game frame's last strip. */
static void VBR_FinishFrame(void)
{
   if(!VBR_Drawing)
      return;

   VBR_Frame.size = sizeof(VBR_Frame);
   VBR_Frame.api_version = VBR_API_VERSION;
   VBR_Frame.game_frames = VBR_Published.game_frames + 1;
   VBR_Frame.budget_cycles = VBR_DISPLAY_FRAME_CYCLES * (VBR_Frame.frmcyc + 1);
   VBR_Frame.chr_writes = VBR_WritesLast[VBR_W_CHR];
   VBR_Frame.bgmap_writes = VBR_WritesLast[VBR_W_BGMAP];
   VBR_Frame.world_writes = VBR_WritesLast[VBR_W_WORLD];
   VBR_Frame.oam_writes = VBR_WritesLast[VBR_W_OAM];
   VBR_Frame.fb_writes = VBR_WritesLast[VBR_W_FB];
   VBR_Frame.reg_writes = VBR_WritesLast[VBR_W_REG];
   VBR_Published = VBR_Frame;
   VBR_Drawing = false;
}

size_t VBR_GetFrameStats(struct vbr_frame_stats *out, size_t size)
{
   struct vbr_frame_stats copy = VBR_Published;
   const size_t n = size < sizeof(copy) ? size : sizeof(copy);

   copy.size = (uint32)n;
   copy.api_version = VBR_API_VERSION;
   if(out && n)
      memcpy(out, &copy, n);
   return sizeof(copy);
}
