/* parity.c: prove a core build emulates identically to another.
 *
 * Loads a libretro core by path, runs a ROM for N frames with a fixed input
 * schedule (the Game Reference Library smoke inputs, which reach menus and
 * gameplay in most retail titles), and prints FNV-1a hashes of every video
 * frame, all audio samples and the final system RAM. Two builds are identical
 * for this ROM when the printed hashes match. It never calls the vbr API, so
 * instrumentation cannot influence the run it is checking.
 *
 *   cc -O2 -I libretro-common/include tools/vbr/parity.c -o parity
 *   ./parity path/to/core.dylib game.vb [frames]
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

static uint64_t video_hash = 1469598103934665603ull, audio_hash = 1469598103934665603ull;
static unsigned frame, video_frames;
static uint64_t audio_frames;

static uint64_t fnv(uint64_t h, const void *data, size_t n)
{
   const unsigned char *p = data;
   while(n--) { h ^= *p++; h *= 1099511628211ull; }
   return h;
}

static bool environment(unsigned cmd, void *data)
{
   switch(cmd)
   {
      case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
      default: return false;
   }
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
   unsigned y;
   uint32_t dims[2] = { w, h };
   video_hash = fnv(video_hash, dims, sizeof dims);
   if(data)
      for(y = 0; y < h; y++)
         video_hash = fnv(video_hash, (const char *)data + y * pitch, (size_t)w * 4);
   video_frames++;
}

static size_t audio_batch(const int16_t *data, size_t n)
{
   audio_hash = fnv(audio_hash, data, n * 4);
   audio_frames += n;
   return n;
}

static void audio_sample(int16_t l, int16_t r) { int16_t a[2] = { l, r }; audio_batch(a, 1); }
static void poll(void) {}

static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{
   if(port || device != RETRO_DEVICE_JOYPAD || index)
      return 0;
   if(id == RETRO_DEVICE_ID_JOYPAD_START)
      return (frame >= 300 && frame < 310) || (frame >= 900 && frame < 910) || (frame >= 1500 && frame < 1510) ||
             (frame >= 2100 && frame < 2110) || (frame >= 2700 && frame < 2710) || (frame >= 3600 && frame < 3610);
   if(id == RETRO_DEVICE_ID_JOYPAD_A)
      return (frame >= 600 && frame < 610) || (frame >= 1200 && frame < 1210) ||
             (frame >= 1800 && frame < 1810) || (frame >= 2400 && frame < 2410) ||
             (frame >= 3000 && frame < 3010) || (frame >= 3300 && frame < 3310) ||
             (frame >= 4800 && frame < 5100 && frame % 60 < 10);
   if(id == RETRO_DEVICE_ID_JOYPAD_RIGHT) return frame >= 4000 && frame < 4150;
   if(id == RETRO_DEVICE_ID_JOYPAD_LEFT) return frame >= 4500 && frame < 4650;
   return 0;
}

#define SYM(name) name##_t name = (name##_t)dlsym(core, #name); if(!name) { fprintf(stderr, "missing %s\n", #name); return 2; }
typedef void (*retro_set_environment_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_init_t)(void);
typedef bool (*retro_load_game_t)(const struct retro_game_info *);
typedef void (*retro_set_controller_port_device_t)(unsigned, unsigned);
typedef void (*retro_run_t)(void);
typedef void *(*retro_get_memory_data_t)(unsigned);
typedef size_t (*retro_get_memory_size_t)(unsigned);
typedef void (*retro_unload_game_t)(void);
typedef void (*retro_deinit_t)(void);

int main(int argc, char **argv)
{
   unsigned frames = argc > 3 ? (unsigned)atoi(argv[3]) : 6000;
   void *core, *bytes, *ram;
   long size;
   size_t ram_size;
   FILE *f;
   struct retro_game_info game;

   if(argc < 3) { fprintf(stderr, "usage: parity core.dylib game.vb [frames]\n"); return 2; }
   core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if(!core) { fprintf(stderr, "%s\n", dlerror()); return 2; }
   SYM(retro_set_environment) SYM(retro_set_video_refresh) SYM(retro_set_audio_sample)
   SYM(retro_set_audio_sample_batch) SYM(retro_set_input_poll) SYM(retro_set_input_state)
   SYM(retro_init) SYM(retro_load_game) SYM(retro_set_controller_port_device) SYM(retro_run)
   SYM(retro_get_memory_data) SYM(retro_get_memory_size) SYM(retro_unload_game) SYM(retro_deinit)

   f = fopen(argv[2], "rb");
   if(!f) { perror(argv[2]); return 2; }
   fseek(f, 0, SEEK_END); size = ftell(f); rewind(f);
   bytes = malloc((size_t)size);
   if(!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) return 2;
   fclose(f);

   retro_set_environment(environment); retro_set_video_refresh(video);
   retro_set_audio_sample(audio_sample); retro_set_audio_sample_batch(audio_batch);
   retro_set_input_poll(poll); retro_set_input_state(input); retro_init();
   game.path = argv[2]; game.data = bytes; game.size = (size_t)size; game.meta = NULL;
   if(!retro_load_game(&game)) { fprintf(stderr, "load failed\n"); return 1; }
   retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
   for(frame = 0; frame < frames; frame++)
      retro_run();
   ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
   ram_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
   printf("{\"frames\":%u,\"video_frames\":%u,\"video\":\"%016llx\",\"audio_frames\":%llu,\"audio\":\"%016llx\",\"ram\":\"%016llx\"}\n",
          frames, video_frames, (unsigned long long)video_hash, (unsigned long long)audio_frames,
          (unsigned long long)audio_hash, (unsigned long long)fnv(1469598103934665603ull, ram, ram ? ram_size : 0));
   retro_unload_game(); retro_deinit(); free(bytes);
   return 0;
}
