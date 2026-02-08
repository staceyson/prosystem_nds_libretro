#ifndef _MSC_VER
#include <sched.h>
#endif
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <boolean.h>
#include <streams/file_stream.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#pragma pack(1)
#endif

#include <libretro.h>
#include "libretro_core_options.h"

#include "Cartridge.h"
#include "Database.h"
#include "Maria.h"
#include "Palette.h"
#include "Pokey.h"
#include "Region.h"
#include "ProSystem.h"
#include "Tia.h"
#include "Memory.h"
#include "Rect.h"

// Rocket - arbitrarily assume we are faster than the DS-Lite?  Actually I found later in many cases the
//          DS_LITE code is more generically appropriate.
u8 isDS_LITE = FALSE;

// Rocket - on the NDS version you can skip BIOS by holding start at startup.  A core option can set this.
u8 bSkipBIOS = FALSE;

// Add ability for the core to completely skip savestate logic for low performing targets.
static u8 skipSaveStates = FALSE;

// Add core variable based palette temp setting.
static u8 paletteTemp = -1; 

// Rocket - on the NDS version you can skip the settings database load by holding select at startup.
//         The A7800DS.dat external file load is already disabled for this core.  Eventually any
//         useful settings in that file could be made core settings, then using a game 
//         content override could have the same impact.  This could still be used to not use
//         the internal database of values which is still in place.  See Database.c
u8  bNoDatabase = FALSE;

//Rocket - SNES adaptor is a physical adapter that lets you hook a SNES controller to the 7800.
//         it's a don't care for libretro.
u32 snes_adaptor = FALSE;

// This is a pointer into the tia sound buffer to show where things are in a circular buffer.
// In libretro we don't have an asynchronous audio callback and so don't need a circular buffer.
//u32 myTiaBufIdx = 0;

// This cheats the checks for wrapping around and bumping into unprocessed data in the old circular buffer.
u32 myTiaBufIdx = 10000;  // Arbitrarily big.

// Rocket - Decided it will be better to leave the circular buffer logic in place and use the workaround
//          hacks (like above) that are already functioning.  The reasoning is that fewer changes from the
//          original A7800DS code are better since maybe need to port future additional changes.
//          That probably should have been a consideration during the initial changes as well!

// To further explain - in NDS version the sound buffer looks like a circular buffer driven by a 
//   timer callback.  myTiaBufIdx shows where the readout has reached and wraps at the
//   end of the buffer back to zero so you read until your myTiaBufIdx hits "tiaBufIdx" which is the index
//   of the data being written into the buffer.  This happens in a7800Utils.c in "OurSoundMixer".
//   When first ported, there was no sound.  This was because:
//   With myTiaBufIdx stuck at zero by default, when the sound pointer wraps to zero
//   Sound was "paused" because that condition of reaching myTiaBufIdx is checked in the sound code in tia.c and pokey.c
//   So either readout needs to handle the circular buffer aspect (and update myTiaBufIdx like OurSoundMixer
//   did), or we need to work around the circular buffer concept, since we will be draining it once per frame
//   anyway - in a Libretro core it's not an asynch callback like on the NDS emulator.

//Rocket - this is used for an FPS display in the NDS emulator.  Don't care for Libretro core since front-ends
//         have FPS capabilities, but it's also used for frameskipping logic, which could be useful - leave it in.
u16 gTotalAtariFrames = 0;

// This determines the amount of frameskip.  Added as a core option.
u8 frameSkipMask = 0xFF;

#ifdef _3DS
extern void* linearMemAlign(size_t size, size_t alignment);
extern void linearFree(void* mem);
#endif

#define VIDEO_BUFFER_SIZE (320 * 292 * 4)
static uint8_t *videoBuffer            = NULL;
static uint8_t videoPixelBytes         = 2;
static int videoWidth                  = 320;
static int videoHeight                 = 240;
static uint32_t display_palette32[256] = {0};
static uint16_t display_palette16[256] = {0};
static uint8_t keyboard_data[17]       = {0};

// Rocketfan - I think this is a "real" video buffer for the NDS implementation, but here it's just a buffer 
//          we can hold the video in until it is copied out to the front end.
unsigned short libretroVidBuf[VIDEO_BUFFER_SIZE];
unsigned short *bufVideo = &libretroVidBuf[0];

#define GAMEPAD_ANALOG_THRESHOLD 0x4000
static bool gamepad_dual_stick_hack    = false;

#define CTRL_ACT2_NORMAL 0
#define CTRL_ACT2_UP     1
#define CTRL_ACT2_FIRE2  2
#define CTRL_ACT2_START  3

static int CTRL_action2 = CTRL_ACT2_NORMAL;  // Default to normal for right fire button
static int CTRL_swapactions = false;

/* Required buffer size is exactly TIA_BUFFER_SIZE,
 * but round up to nearest multiple of 128 for
 * peace of mind... */
#define AUDIO_SAMPLE_BUFFER_SIZE ((SNDLENGTH + 0x7F) & ~0x7F)
static uint8_t *pokeyMixBuffer         = NULL;
static int16_t *audioOutBuffer         = NULL;

/* Low pass audio filter */
static bool low_pass_enabled           = false;
static int32_t low_pass_range          = 0;
static int32_t low_pass_prev           = 0; /* Previous sample */

/* Save state size was determined empirically.  */
#define SAVE_STATE_SIZE 165000

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;

static bool libretro_supports_bitmasks = false;

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;

//  Rocket - refer to https://docs.rs/rust-libretro/latest/rust_libretro/environment/fn.set_content_info_override.html
//  need_fullpath true implies the front-end will then assume the core loads it's own ROM, which this core does.

   static const struct retro_system_content_info_override content_overrides[] = {
      {
         "a78|bin|cdf", /* extensions */
         true, /* need_fullpath - true guarantees the path is valid. */
         false /* persistent_data - Cartridge.c has a ROM buffer - we don't need it. */
      },
      { NULL, false, false }
   };

   environ_cb = cb;
   libretro_set_core_options(environ_cb);
   environ_cb(RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE,
         (void*)content_overrides);

   vfs_iface_info.required_interface_version = 1;
   vfs_iface_info.iface                      = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);
}

// Rocketfan - needs an extra parameter, since pitch of source is not width in this implementation.

#define BLIT_VIDEO_BUFFER(typename_t, src, palette, src_bump, width, height, pitch, dst) \
   {                                                                           \
      typename_t *surface = (typename_t*)dst;                                  \
      uint32_t x, y;                                                           \
                                                                               \
      for(y = 0; y < height; y++)                                              \
      {                                                                        \
         typename_t *surface_ptr = surface;                                    \
         const uint8_t *src_ptr  = src;                                        \
                                                                               \
         for(x = 0; x < width; x++)                                            \
            *(surface_ptr++) = *(palette + *(src_ptr++));                      \
                                                                               \
         surface += pitch;                                                     \
         src     += src_bump;                                                  \
      }                                                                        \
   }

static void display_ResetPalette(void)
{
   unsigned index;

   for(index = 0; index < 256; index++)
   {
      uint32_t r = palette_data[(index * 3) + 0] << 16;
      uint32_t g = palette_data[(index * 3) + 1] << 8;
      uint32_t b = palette_data[(index * 3) + 2];
      display_palette32[index] = r | g | b;
      display_palette16[index] = ((r & 0xF80000) >> 8) |
                                 ((g & 0x00F800) >> 5) |
                                 ((b & 0x0000F8) >> 3);
   }
}

static short sound_Lerp(short a, short b, float t) {
   return (short)floorf((float)a + (float)(b - a) * t + 0.5f);
}

// Rocket - no bupchip support in the NDS version.
#if 0
static void sound_ResampleBupChip(const short* source, short* target, int length)
{
   int targetIndex;
   uint32_t bupchipBufferSize = CORETONE_BUFFER_SAMPLES * 4;

   for(targetIndex = 0; targetIndex < length; targetIndex++)
   {
      float t;
      int channel;
      float sourceIndex = (float)targetIndex / (float)length * (float)bupchipBufferSize;
      uint32_t sourceLo = (uint32_t)floorf(sourceIndex), sourceHi = (uint32_t)ceilf(sourceIndex);
      if(sourceHi >= bupchipBufferSize)
         sourceHi = bupchipBufferSize;
      t = sourceIndex - (float)sourceLo;

      for (channel = 0; channel < 2; channel++)
      {
         int sample = sound_Lerp(source[sourceLo * 2 + channel], source[sourceHi * 2 + channel], t);
         sample += target[targetIndex * 2 + channel];
         if(sample > INT16_MAX)
            sample = INT16_MAX;
         else if(sample < INT16_MIN)
            sample = INT16_MIN;
         target[targetIndex * 2 + channel] = sample;
      }
   }
}
#endif


static void sound_Store(void)
{
   u16 *tia_samples_buf = tia_buffer;
   int16_t *audio_out_buf   = audioOutBuffer;
   size_t i, j;
   static int startSoundSkip=0;


   // Rocketfan - We have 2 samples per verticle line, end of samples is stored in tiaBufIdx.
   u32 endIdx = get_tiaBufIdx();

   // Rocket - In the original Libretro code pokey was mixed in here.  The sound is alreaady 
   //          mixed by now in the A7800DS implementation.
      
   //Rocketfan - In the NDS version we have two 8 bit samples per 16 bit word.  Mod accordingly.
   //            Enabling low_pass seemed OK, basic on a test of one game.

   /* Convert 8u mono to 16s stereo, applying
    * low pass filter if enabled */
   if (low_pass_enabled)
   {
      /* Restore previous sample */
      int32_t low_pass = low_pass_prev;

      /* Single-pole low-pass filter (6 dB/octave) */
      int32_t factor_a = low_pass_range;
      int32_t factor_b = 0x10000 - factor_a;

      for(i = 0; i < endIdx; i++)
      {
         int16_t sample_16 = (int16_t)(*(tia_samples_buf) << 8);

         /* Apply low-pass filter */
         low_pass = (low_pass * factor_a) + (sample_16 * factor_b);

         /* 16.16 fixed point */
         low_pass >>= 16;

         /* Update sound buffer */
         *(audio_out_buf++) = (int16_t)low_pass;
         *(audio_out_buf++) = (int16_t)low_pass;
         
         sample_16 = (int16_t)((*(tia_samples_buf++) & (u16)0xFF00));

         /* Apply low-pass filter */
         low_pass = (low_pass * factor_a) + (sample_16 * factor_b);

         /* 16.16 fixed point */
         low_pass >>= 16;

         /* Update sound buffer */
         *(audio_out_buf++) = (int16_t)low_pass;
         *(audio_out_buf++) = (int16_t)low_pass;
      }

      /* Save last sample for next frame */
      low_pass_prev = low_pass;
   }
   else
   {
      for(i = 0; i < endIdx; i++)
      {
         
         int16_t sample_16_1 = (int16_t)(*(tia_samples_buf) << 8);
         int16_t sample_16_2 = (int16_t)((*(tia_samples_buf++) & (u16)0xFF00));
         

         *(audio_out_buf++) = sample_16_1;
         *(audio_out_buf++) = sample_16_1;
         *(audio_out_buf++) = sample_16_2;
         *(audio_out_buf++) = sample_16_2;
      }
   }

#if 0
   //  Rocket - Rikki and Vikki is not (currently) supported by this core.  Flashbacks (or Retroarch?)
   //           can use the original Prosystem core for that game.
   /* Mix in sound generated by BupChip
    * ("Rikki & Vikki") */
   if(cartridge_bupchip)
      sound_ResampleBupChip(bupchip_buffer, audioOutBuffer, tia_size);
#endif

   if (startSoundSkip < 30 ) {
       // Don't send out the glitchy first 0.5 seconds of sound.
       startSoundSkip++;
   } else {
       audio_batch_cb(audioOutBuffer, (endIdx << 1));
   }
   // Start Tia back at the start of sound buffer (It is NOT a circular buffer for the libretro core.) 
   tia_Reset_Idx();
}

static void update_input(void)
{
   unsigned i,j;
   unsigned joypad_bits[2];
   unsigned j2_override_right = 0;
   unsigned j2_override_left  = 0;
   unsigned j2_override_down  = 0;
   unsigned j2_override_up    = 0;

    
   /*
    * ----------------------------------------------------------------------------
    * SetInput
    * +----------+--------------+-------------------------------------------------
    * | Offset   | Controller   | Control
    * +----------+--------------+-------------------------------------------------
    * | 00       | Joystick 1   | Right
    * | 01       | Joystick 1   | Left
    * | 02       | Joystick 1   | Down
    * | 03       | Joystick 1   | Up
    * | 04       | Joystick 1   | Button 1
    * | 05       | Joystick 1   | Button 2
    * | 06       | Joystick 2   | Right
    * | 07       | Joystick 2   | Left
    * | 08       | Joystick 2   | Down
    * | 09       | Joystick 2   | Up
    * | 10       | Joystick 2   | Button 1
    * | 11       | Joystick 2   | Button 2
    * | 12       | Console      | Reset
    * | 13       | Console      | Select
    * | 14       | Console      | Pause
    * | 15       | Console      | Left Difficulty
    * | 16       | Console      | Right Difficulty
    * +----------+--------------+-------------------------------------------------
    */

   input_poll_cb();

   if (libretro_supports_bitmasks)
   {
      for (j = 0; j < 2; j++)
         joypad_bits[j] = input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
   }
   else
   {
      for (j = 0; j < 2; j++)
      {
         joypad_bits[j] = 0;
         for (i = 0; i < (RETRO_DEVICE_ID_JOYPAD_R3+1); i++)
            joypad_bits[j] |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
      }
   }

   /* If dual stick controller hack is enabled,
    * fetch overrides for player 2's joystick
    * right/left/down/up values */
   if (gamepad_dual_stick_hack)
   {
      int analog_x = input_state_cb(0, RETRO_DEVICE_ANALOG,
            RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
      int analog_y = input_state_cb(0, RETRO_DEVICE_ANALOG,
            RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

      if (analog_x >= GAMEPAD_ANALOG_THRESHOLD)
         j2_override_right = 1;
      else if (analog_x <= -GAMEPAD_ANALOG_THRESHOLD)
         j2_override_left  = 1;

      if (analog_y >= GAMEPAD_ANALOG_THRESHOLD)
         j2_override_down  = 1;
      else if (analog_y <= -GAMEPAD_ANALOG_THRESHOLD)
         j2_override_up    = 1;
   }

   keyboard_data[0]  = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)  ? 1 : 0;
   keyboard_data[1]  = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)   ? 1 : 0;
   keyboard_data[2]  = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)   ? 1 : 0;
   keyboard_data[3]  = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_UP)     ? 1 : 0;
   keyboard_data[4]  = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_B)      ? 1 : 0;
   keyboard_data[5]  = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_A)      ? 1 : 0;

   keyboard_data[6]  = joypad_bits[1] & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)  ? 1 : j2_override_right;
   keyboard_data[7]  = joypad_bits[1] & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)   ? 1 : j2_override_left;
   keyboard_data[8]  = joypad_bits[1] & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)   ? 1 : j2_override_down;
   keyboard_data[9]  = joypad_bits[1] & (1 << RETRO_DEVICE_ID_JOYPAD_UP)     ? 1 : j2_override_up;
   keyboard_data[10] = joypad_bits[1] & (1 << RETRO_DEVICE_ID_JOYPAD_B)      ? 1 : 0;
   keyboard_data[11] = joypad_bits[1] & (1 << RETRO_DEVICE_ID_JOYPAD_A)      ? 1 : 0;

   keyboard_data[12] = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_X)      ? 1 : 0;
   keyboard_data[13] = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT) ? 1 : 0;
   keyboard_data[14] = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_START)  ? 1 : 0;
   keyboard_data[15] = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_L)      ? 1 : 0;
   keyboard_data[16] = joypad_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_R)      ? 1 : 0;


   if (CTRL_action2 == CTRL_ACT2_UP) { 
       // Makes sense for bubble-bobble with one button joystick.
       keyboard_data[5]  = keyboard_data[3];
       keyboard_data[11] = keyboard_data[9];
   } else if (CTRL_action2 == CTRL_ACT2_FIRE2) {
       // Use J2 fire button for J1 action 2 and vice-versa.
       // Won't make sense in two-player-simultaneous games! 
       keyboard_data[5]  = keyboard_data[10];
       keyboard_data[11] = keyboard_data[4];
   } else if (CTRL_action2 == CTRL_ACT2_START) {
       // Really only makes sense for Joystick 1 and single player games, but sacrifice pause.
       keyboard_data[5]  = keyboard_data[14];
       keyboard_data[14] = 0;
   }
 
   if (CTRL_swapactions) {
       int hold = keyboard_data[4];
       keyboard_data[4] = keyboard_data[5];
       keyboard_data[5] = hold;
 
       hold = keyboard_data[10];      
       keyboard_data[10] = keyboard_data[11];
       keyboard_data[11] = hold;
   }
}

void check_variables(bool first_run)
{
   struct retro_variable var = {0};

   /* Only read colour depth option on first run */
   if (first_run)
   {
      var.key   = "prosystem_color_depth";
      var.value = NULL;

      /* Set 16bpp by default */
      videoPixelBytes = 2;

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         if (strcmp(var.value, "24bit") == 0)
            videoPixelBytes = 4;
   }

   /* Read low pass audio filter settings */
   var.key   = "prosystem_low_pass_filter";
   var.value = NULL;

   low_pass_enabled = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      if (strcmp(var.value, "enabled") == 0)
         low_pass_enabled = true;

   var.key   = "prosystem_low_pass_range";
   var.value = NULL;

   low_pass_range = (60 * 0x10000) / 100;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		low_pass_range = (strtol(var.value, NULL, 10) * 0x10000) / 100;

   /* Read dual stick controller setting */
   var.key   = "prosystem_gamepad_dual_stick_hack";
   var.value = NULL;

   gamepad_dual_stick_hack = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      if (strcmp(var.value, "enabled") == 0)
         gamepad_dual_stick_hack = true;

    bSkipBIOS = FALSE;
    var.key = "prosystem_nds_skipbios";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "enabled") == 0)
            bSkipBIOS = TRUE;
    }

    skipSaveStates = FALSE;
    var.key = "prosystem_nds_skipsavestates";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "enabled") == 0)
            skipSaveStates = TRUE;
    }

    frameSkipMask = 0xFF;
    var.key = "prosystem_nds_frameskip";
    var.value = NULL;

// From the original A7800DS code:
    //  if (myCartInfo.frameSkip == FRAMESKIP_MEDIUM)     frameSkipMask = 0x03;
    //  if (myCartInfo.frameSkip == FRAMESKIP_AGGRESSIVE) frameSkipMask = 0x01;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "off") == 0)
            frameSkipMask = 0xFF;
        else if (strcmp(var.value, "medium") == 0)
            frameSkipMask = 0x03;
        else if (strcmp(var.value, "aggressive") == 0)
            frameSkipMask = 0x01;
    }

    paletteTemp = -1;  // -1 indicates no override.  "off" setting for var will leave this as -1.
    var.key = "prosystem_nds_palettetemp";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "cool") == 0)
            paletteTemp = 0;
        else if (strcmp(var.value, "warm") == 0)
            paletteTemp = 1;
        else if (strcmp(var.value, "hot") == 0)
            paletteTemp = 2;
    }

/*

Rocketfan - the following two core variables are intended primarily for the
            Atari flashback devices which have only a single action button to 
            provide workarounds for some games or for people willing to build
            a custom controller.  Not really usful for most other controllers.

Action2 controls can be: 
   off=normal operation
   up=up on joystick performs second action
   fire2=opposite joystick fire button is the second action
   start=start button does the second action, sacrifices pause
*/

    var.key = "prosystem_nds_action2";
    var.value = NULL;

    CTRL_action2 = CTRL_ACT2_NORMAL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "off") == 0)
            CTRL_action2 = CTRL_ACT2_NORMAL;
        else if (strcmp(var.value, "up") == 0)
            CTRL_action2 = CTRL_ACT2_UP;
        else if (strcmp(var.value, "fire2") == 0)
            CTRL_action2 = CTRL_ACT2_FIRE2;
        else if (strcmp(var.value, "start") == 0)
            CTRL_action2 = CTRL_ACT2_START;
    }

    CTRL_swapactions = false;

    var.key = "prosystem_nds_swapactions";
    var.value = NULL;
   
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "enabled") == 0)
            CTRL_swapactions = true;
    }

}

/************************************
 * libretro implementation
 ************************************/

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "ProSystem";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = "1.3e" GIT_VERSION;
   info->need_fullpath    = false;
   info->valid_extensions = "a78|bin|cdf";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            =  REGION_FREQUENCY_NTSC;

// Rocketfan - this results in a "non-standard" audio sample rate of 31440 - not 44100 or... But it works.
   info->timing.sample_rate    = (REGION_FREQUENCY_NTSC * REGION_SCANLINES_NTSC) << 1; /* 2 samples per scanline */
   info->geometry.base_width   = videoWidth;
   //info->geometry.base_height  = 223;
   info->geometry.base_height  = 233;
   info->geometry.max_width    = 320;
   info->geometry.max_height   = 292;
   info->geometry.aspect_ratio = 4.0 / 3.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}


// Rocket - returning zero from this supposedly (?) indicates to frontend that savestates are not supported.
size_t retro_serialize_size(void) 
{
       if (skipSaveStates)
           return 0;

       return SAVE_STATE_SIZE;   
}

bool retro_serialize(void *data, size_t size)
{
   if (size < SAVE_STATE_SIZE  || skipSaveStates)
      return false;

   return prosystem_Save((char*)data);
}

bool retro_unserialize(const void *data, size_t size)
{
   if (size < SAVE_STATE_SIZE  || skipSaveStates)
      return false;
   return prosystem_Load((const char*)data);
}


void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}



bool retro_load_game(const struct retro_game_info *info)
{
   enum retro_pixel_format fmt;
   char biospath[512];
   const char *system_directory_c             = NULL;
   const struct retro_game_info_ext *info_ext = NULL;
#ifdef _WIN32
   char slash = '\\';
#else
   char slash = '/';
#endif

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Console Reset" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Console Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Console Pause" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Left Difficulty" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Right Difficulty" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "(Dual Stick) P2 X-Axis" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "(Dual Stick) P2 Y-Axis" },

      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "1" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "2" },

      { 0 },
   };

   if (!info)
      return false;

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   /* Set color depth */
   check_variables(true);

   if (videoPixelBytes == 4)
   {
      fmt = RETRO_PIXEL_FORMAT_XRGB8888;
      if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      {
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "[ProSystem]: XRGB8888 is not supported - trying RGB565...\n");

         /* Fallback to RETRO_PIXEL_FORMAT_RGB565 */
         videoPixelBytes = 2;
      }
   }

   if (videoPixelBytes == 2)
   {
      fmt = RETRO_PIXEL_FORMAT_RGB565;
      if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      {
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "[ProSystem]: RGB565 is not supported.\n");
         return false;
      }

   }

   memset(keyboard_data, 0, sizeof(keyboard_data));

   /* Difficulty switches: 
    * Left position = (B)eginner, Right position = (A)dvanced
    * Left difficulty switch defaults to left position, "(B)eginner"
    */
   keyboard_data[15] = 1;
   /* Right difficulty switch defaults to right position,
    * "(A)dvanced", which fixes Tower Toppler
    */
   keyboard_data[16] = 0;

// Rocket - The NDS implementation relies on some modifications to the ROM buffer.  So be it! 
//          Cartridge.c will read the ROM and also make the internal persistent copy of the ROM. 

    if (!cartridge_Load(info->path))
      return false;

   database_Load(cartridge_digest);

   environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory_c);

//  Rocketfan - Note that system_dir is always provided as /tmp on Atari flashbacks - weird.
//    Also, I believe BIOS is supported by NDS, but I'm not sure of the benefit, as every game I 
//    tried (so far) works OK?  ALSO - PAL is not supported by this core!  Must use NTSC carts.
//    That was a limitation of the NDS version, and still is.
   
   if (!bSkipBIOS) {
       sprintf(biospath, "%s%c%s", system_directory_c, slash, "7800 BIOS (U).rom");
       bios_check_and_load(biospath);
   }

   // If set, override the palette temp for the cartridge from the core variable.
   // prosystem_Reset below indirectly loads the palette.
   if (paletteTemp >= 0) {
      myCartInfo.palette = paletteTemp;
   }

   prosystem_Reset();

// Rocket - This loads the palette LUTs used by the BLITing macro - so the main or basic
//          palette functionality is still intact.  One effect of prosystem_reset above is
//          To load the Palette via Region_reset.  In the case of A7800DS this is influenced by
//          information in an A7800DS.dat settings file with optional settings per game which is
//          currently disabled in this core.  That settings file can have a cool/warm/hot palette
//          setting per game.  For libretro this can be set by a core variable and per-game-override.
   display_ResetPalette();

   return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   (void)game_type;
   (void)info;
   (void)num_info;
   return false;
}

void retro_unload_game(void) 
{
   prosystem_Close();
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned id)
{
    if ( id == RETRO_MEMORY_SYSTEM_RAM )
        return memory_ram;
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    if ( id == RETRO_MEMORY_SYSTEM_RAM )
        return MEMORY_SIZE;
    return 0;
}

void retro_init(void)
{
   struct retro_log_callback log;
   unsigned level = 5;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   // Rocketfan - This is probably NOT supported on flashbacks with their old-school front-ends.
   // True - this was causing a crash/exit by the flashback's "retroplayer" front end!!
   // environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

#ifdef _3DS
   videoBuffer = (uint8_t*)linearMemAlign(VIDEO_BUFFER_SIZE * sizeof(uint8_t), 128);
#else
   videoBuffer = (uint8_t*)malloc(VIDEO_BUFFER_SIZE * sizeof(uint8_t));
#endif

   pokeyMixBuffer = (uint8_t*)malloc(AUDIO_SAMPLE_BUFFER_SIZE * sizeof(uint8_t));
   /* Samples are mono, output buffer is stereo
    * (AUDIO_SAMPLE_BUFFER_SIZE * 2) */
   audioOutBuffer = (int16_t*)malloc((AUDIO_SAMPLE_BUFFER_SIZE << 1) * sizeof(int16_t));

}

void retro_deinit(void)
{
   libretro_supports_bitmasks = false;
   gamepad_dual_stick_hack    = false;
   low_pass_enabled           = false;
   low_pass_prev              = 0;

   if (videoBuffer)
   {
#ifdef _3DS
      linearFree(videoBuffer);
#else
      free(videoBuffer);
#endif
      videoBuffer = NULL;
   }

   if (pokeyMixBuffer)
   {
      free(pokeyMixBuffer);
      pokeyMixBuffer = NULL;
   }

   if (audioOutBuffer)
   {
      free(audioOutBuffer);
      audioOutBuffer = NULL;
   }
}

void retro_reset(void)
{
    prosystem_Reset();
}

static INLINE uint32_t Rect_GetHeight(struct Rects *rect)
{
   return (rect->bottom - rect->top) + 1;
}

void retro_run(void)
{
   const uint8_t *buffer = NULL;
   uint32_t video_pitch  = 320;
   bool options_updated  = false;
   // rect maria_visibleArea = {0, 26, 319, 248};
   rect maria_visibleArea = {0, 16, 319, 248};  // Try for 16 to 248 "inclusive" or 233 lines.
   rect maria_displayArea = {0, 16, 319, 258};


// Rocketfan - Retro_run is called once per frame.  It should process a frame of video/audio and check inputs.

   /* Core options */
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &options_updated) && options_updated)
      check_variables(false);

   update_input();

   prosystem_ExecuteFrame(keyboard_data); /* wants input */

   videoWidth  = Rect_GetLength(&maria_visibleArea);
   videoHeight = Rect_GetHeight(&maria_visibleArea);

// Rocketfan - looking at Maria code, in this version the Maria surface is always moved ahead 256 words or 
// 512 bytes per line (words are 16bits here at least).  Furthermore, the source pointer "buffer" is passed
// as a byte pointer, so the macro below needs an extra parameter.

   buffer = (uint8_t *)(maria_surface + ((maria_visibleArea.top - maria_displayArea.top) * 256));

   if (videoPixelBytes == 2)
   {
      BLIT_VIDEO_BUFFER(uint16_t, buffer, display_palette16, 512, videoWidth, videoHeight, video_pitch, videoBuffer);
   }
   else
   {
      BLIT_VIDEO_BUFFER(uint32_t, buffer, display_palette32, 512, videoWidth, videoHeight, video_pitch, videoBuffer);
   }

   video_cb(videoBuffer, videoWidth, videoHeight, videoWidth * videoPixelBytes);


   sound_Store();
   
}
