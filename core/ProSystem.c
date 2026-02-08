// ----------------------------------------------------------------------------
//   ___  ___  ___  ___       ___  ____  ___  _  _
//  /__/ /__/ /  / /__  /__/ /__    /   /_   / |/ /
// /    / \  /__/ ___/ ___/ ___/   /   /__  /    /  emulator
//
// ----------------------------------------------------------------------------
// Copyright 2003, 2004 Greg Stanton
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
// ----------------------------------------------------------------------------
// ProSystem.cpp
// ----------------------------------------------------------------------------

#include "ProSystem.h"
#include "Database.h"

extern u8 isDS_LITE;
extern u8 frameSkipMask;

uint prosystem_cycles __attribute__((section(".dtcm"))) = 0;
uint32 bg32           __attribute__((section(".dtcm"))) = 0;
uint bRenderFrame     __attribute__((section(".dtcm"))) = 0;


#define CYCLES_BEFORE_DMA      28     // Number of cycles before DMA kicks in (really 7 CPU cycles)
#define CYCLES_PER_SCANLINE   454     // 454 Cycles per Scanline in an NTSC system (really 113.5 CPU cycles)


// ----------------------------------------------------------------------------
// Reset
// ----------------------------------------------------------------------------
void prosystem_Reset()
{
    if(cartridge_IsLoaded())
    {
        sally_Reset();
        region_Reset();
        tia_Clear();
        tia_Reset();
        pokey_Clear();
        pokey_Reset();
        memory_Reset();
        maria_Clear();
        maria_Reset();
        riot_Reset();
        cartridge_LoadHighScoreCart();

        cartridge_Store(); // Always call this - it may setup some RAM or other stuff below the BIOS region...

        // Load 7800 BIOS if available... otherwise direct load the CART
        if(bios_available && !bSkipBIOS)
        {
            bios_Store();
            bios_show_counter = myCartInfo.biosTimeout;
        }

        prosystem_cycles = sally_ExecuteRES();
    }
}


// ----------------------------------------------------------------------------
// ExecuteFrame - this is hand-tuned for NTSC output with hard-coded
// NTSC frame values ... this will not work properly if a PAL ROM is used.
// ----------------------------------------------------------------------------
ITCM_CODE void prosystem_ExecuteFrame(const byte * input)
{
    extern u16 gTotalAtariFrames;
    extern word * framePtr;
    extern uint maria_cycles;

    gTotalAtariFrames++;
    bRenderFrame = 0;

    riot_SetInput(input);

    // ---------------------------------------------------------------------
    // Handle the VERTICAL BLANK area first... speeds up processing below...
    // ---------------------------------------------------------------------
    memory_ram[MSTAT] = 128; // Into the Vertical Blank...  

    // -------------------------------------------------------------------------------------------  
    // Note: this is not accurate. It should be 263 scanlines total but it doesn't work for all
    // games at 263 so we've gone with 262 for maximum compatibility. This is likely due to some
    // emulation cycle counting inaccuracies. At 263, some games run too fast (Asteroids, Deluxe) 
    // and some crash (Robotron) and some issues with Pole Position II and the joystick selection
    // of a course to play. I've also seen graphical glitches in Crossbow when loaded via the 
    // BIOS and all kinds of graphical oddities in Xenophobe. Be very careful if you change this...
    // be sure you understand the consequences (and this developer doesn't... so be warned!).
    // -------------------------------------------------------------------------------------------    
    for(maria_scanline = 1; maria_scanline <= 21; maria_scanline++)
    {
        prosystem_cycles = 0;

        if(maria_scanline == 21) // Maria can start to do her thing... We've had 20 VBLANK scanlines
        {
            memory_ram[MSTAT] = 0; // Out of the vertical blank
            framePtr = (word * )(maria_surface);
            sally_Execute(CYCLES_BEFORE_DMA);

            maria_RenderScanlineTOP();

            // Cycle Stealing happens here...
            prosystem_cycles += ((maria_cycles + 3) >> 2) << 2; // Always a multiple of 4
            if(riot_and_wsync & 2) riot_UpdateTimer(maria_cycles >> 2);
        }
        else
        {
            sally_Execute(CYCLES_BEFORE_DMA);
        }

        sally_Execute(CYCLES_PER_SCANLINE);

        if(myCartInfo.pokeyType) // If pokey enabled, we process 1 pokey sample and 1 TIA sample. Good enough.
        {
            pokey_Process();
            pokey_Scanline();
        }
        else tia_Process(); // If all we have to deal with is the TIA, we can do so at 31KHz
    }

    // ------------------------------------------------------------
    // Now handle the Main display area...
    // ------------------------------------------------------------
    for(; maria_scanline < 263; maria_scanline++)
    {
        prosystem_cycles = 0;

        if(maria_scanline >= 30)
        {
 
            // --------------------------------------------------------------------------
            // We can start to render the scanlines if we are not skipping this frame.
            // The DS/DSi only has 192 vertical pixels and we can perform some hardware
            // scaling but there is a point at which we really can't show any more lines.
            // To that end, we'll render ~223 lines which is good enough for most games.
            // If a game uses more overscan area - those scanlines won't show on screen.
            // ---------------------------------------------------------------------------

            // Rocketfan - the 223 lines as mentioned above is not enough for some games.
            // A libretro core can render more lines than the NDS, so try for all 233
#if 0
            if(maria_scanline >= 267)
            {
                bRenderFrame = 0; // We can stop rendering frames... DS can't show it anyway with limited vertical resolution.
            }
            else
            {
                bRenderFrame = gTotalAtariFrames & frameSkipMask;
            }
#endif
            bRenderFrame = gTotalAtariFrames & frameSkipMask;

        }

        sally_Execute(CYCLES_BEFORE_DMA);

        maria_RenderScanline();

        // Cycle Stealing happens here...
        prosystem_cycles += ((maria_cycles + 3) >> 2) << 2; // Always a multiple of 4
        if(riot_and_wsync & 2) riot_UpdateTimer(maria_cycles >> 2);

        sally_Execute(CYCLES_PER_SCANLINE);

        if(myCartInfo.pokeyType) // If pokey enabled, we process 1 pokey sample and 1 TIA sample. Good enough.
        {
            pokey_Process();
            pokey_Scanline();
        }
        else tia_Process(); // If all we have to deal with is the TIA, we can do so at 31KHz
    }
}

#if 0

// Rocket - save and load states from original prosystem libretro version - would need rework.  Leave out for now.

bool prosystem_Save(char *buffer, bool fast_saves)
{
   uint32_t size = 0;
   uint32_t index;

   for(index = 0; index < 16; index++)
      buffer[size + index] = PRO_SYSTEM_STATE_HEADER[index];
   size += 16;

   buffer[size++] = 1;
   for(index = 0; index < 4; index++)
      buffer[size + index] = 0;
   size += 4;

   for(index = 0; index < 32; index++)
      buffer[size + index] = cartridge_digest[index];
   size += 32;

   buffer[size++] = sally_a;
   buffer[size++] = sally_x;
   buffer[size++] = sally_y;
   buffer[size++] = sally_p;
   buffer[size++] = sally_s;
   buffer[size++] = sally_pc.b.l;
   buffer[size++] = sally_pc.b.h;
   buffer[size++] = cartridge_bank;

   for(index = 0; index < 16384; index++)
      buffer[size + index] = memory_ram[index];
   size += 16384;

   if (fast_saves)
   {
      if (bios_enabled)
      {
         save_uint32_to_buffer(buffer, &size, bios_size);
         for(index = MEMORY_SIZE - bios_size; index <= MEMORY_SIZE; index++)
            buffer[size + index] = memory_ram[index];
         size += bios_size;
      }
 
      for(index = 0; index < TIA_BUFFER_SIZE; index++)
         buffer[size + index] = tia_buffer[index];
      size += TIA_BUFFER_SIZE;

      for(index = 0; index < 2; index++)
      {
         buffer[size++] = tia_volume[index];
         buffer[size++] = tia_counterMax[index];
         buffer[size++] = tia_counter[index];
         buffer[size++] = tia_audc[index];
         buffer[size++] = tia_audf[index];
         buffer[size++] = tia_audv[index];

         save_uint32_to_buffer(buffer, &size, tia_poly4Cntr[index]);
         save_uint32_to_buffer(buffer, &size, tia_poly5Cntr[index]);
         save_uint32_to_buffer(buffer, &size, tia_poly9Cntr[index]);
      }
      buffer[size++] = tia_soundCntr;

      save_uint32_to_buffer(buffer, &size, pokey_soundCntr);

      for(index = 0; index < 4; index++)
      {
         buffer[size++] = pokey_audf[index];
         buffer[size++] = pokey_audc[index];
         buffer[size++] = pokey_output[index];
         buffer[size++] = pokey_outVol[index];
      }
      buffer[size++] = pokey_audctl;

      save_uint32_to_buffer(buffer, &size, pokey_poly17Size);
      save_uint32_to_buffer(buffer, &size, pokey_polyAdjust);
      save_uint32_to_buffer(buffer, &size, pokey_poly04Cntr);
      save_uint32_to_buffer(buffer, &size, pokey_poly05Cntr);
      save_uint32_to_buffer(buffer, &size, pokey_poly17Cntr);

      for(index = 0; index < 4; index++)
      {
         save_uint32_to_buffer(buffer, &size, pokey_divideMax[index]);
         save_uint32_to_buffer(buffer, &size, pokey_divideCount[index]);
      }

      save_uint32_to_buffer(buffer, &size, pokey_sampleMax);

      for(index = 0; index < 2; index++)
      {
         save_uint32_to_buffer(buffer, &size, pokey_sampleCount[index]);
      }

      save_uint32_to_buffer(buffer, &size, pokey_baseMultiplier);
   }

   if(cartridge_type == CARTRIDGE_TYPE_SUPERCART_RAM)
   {
      for(index = 0; index < 16384; index++)
         buffer[size + index] = memory_ram[16384 + index];
      size += 16384;
   }
   else if(cartridge_type == CARTRIDGE_TYPE_SOUPER)
   {
      buffer[size++] = cartridge_souper_chr_bank[0];
      buffer[size++] = cartridge_souper_chr_bank[1];
      buffer[size++] = cartridge_souper_mode;
      buffer[size++] = cartridge_souper_ram_page_bank[0];
      buffer[size++] = cartridge_souper_ram_page_bank[1];
      for(index = 0; index < sizeof(memory_souper_ram); index++)
         buffer[size + index] = memory_souper_ram[index];
      size += sizeof(memory_souper_ram);
      buffer[size++] = bupchip_flags;
      buffer[size++] = bupchip_volume;
      buffer[size++] = bupchip_current_song;
   }

   return true;
}

bool prosystem_Load(const char *buffer, bool fast_saves)
{
   uint32_t index;
   char digest[33] = {0};
   uint32_t offset = 0;

   for(index = 0; index < 16; index++)
   {
      /* File is not a valid ProSystem save state. */
      if(buffer[offset + index] != PRO_SYSTEM_STATE_HEADER[index])
         return false;
   }
   offset += 16;
   buffer[offset++];

   for(index = 0; index < 4; index++);
   offset += 4;

   for(index = 0; index < 32; index++)
      digest[index] = buffer[offset + index];

   offset += 32;

   /* Does not match loaded cartridge digest? */
   if(strcmp(cartridge_digest, digest) != 0)
      return false;

   sally_a      = buffer[offset++];
   sally_x      = buffer[offset++];
   sally_y      = buffer[offset++];
   sally_p      = buffer[offset++];
   sally_s      = buffer[offset++];
   sally_pc.b.l = buffer[offset++];
   sally_pc.b.h = buffer[offset++];

   cartridge_StoreBank(buffer[offset++]);

   for(index = 0; index < 16384; index++)
      memory_ram[index] = buffer[offset + index];
   offset += 16384;

   if (fast_saves)
   {
      if (bios_enabled)
      {
         bios_size = read_uint32_from_buffer(buffer, &offset);
         for(index = MEMORY_SIZE - bios_size; index <= MEMORY_SIZE; index++)
            memory_ram[index] = buffer[offset + index];
         offset += bios_size;
      }

      for(index = 0; index < TIA_BUFFER_SIZE; index++)
         tia_buffer[index] = buffer[offset + index];
      offset += TIA_BUFFER_SIZE;

      for(index = 0; index < 2; index++)
      {
         tia_volume[index] = buffer[offset++];
         tia_counterMax[index] = buffer[offset++];
         tia_counter[index] = buffer[offset++];
         tia_audc[index] = buffer[offset++];
         tia_audf[index] = buffer[offset++];
         tia_audv[index] = buffer[offset++];
         tia_poly4Cntr[index] = read_uint32_from_buffer(buffer, &offset);
         tia_poly5Cntr[index] = read_uint32_from_buffer(buffer, &offset);
         tia_poly9Cntr[index] = read_uint32_from_buffer(buffer, &offset);
      }
      tia_soundCntr = buffer[offset++];

      pokey_soundCntr = read_uint32_from_buffer(buffer, &offset);

      for(index = 0; index < 4; index++)
      {
         pokey_audf[index] = buffer[offset++];
         pokey_audc[index] = buffer[offset++];
         pokey_output[index] = buffer[offset++];
         pokey_outVol[index] = buffer[offset++];
      }
      pokey_audctl = buffer[offset++];

      pokey_poly17Size = read_uint32_from_buffer(buffer, &offset);
      pokey_polyAdjust = read_uint32_from_buffer(buffer, &offset);
      pokey_poly04Cntr = read_uint32_from_buffer(buffer, &offset);
      pokey_poly05Cntr = read_uint32_from_buffer(buffer, &offset);
      pokey_poly17Cntr = read_uint32_from_buffer(buffer, &offset);

      for(index = 0; index < 4; index++)
      {
         pokey_divideMax[index] = read_uint32_from_buffer(buffer, &offset);
         pokey_divideCount[index] = read_uint32_from_buffer(buffer, &offset);
      }

      pokey_sampleMax = read_uint32_from_buffer(buffer, &offset);

      for(index = 0; index < 2; index++)
      {
         pokey_sampleCount[index] = read_uint32_from_buffer(buffer, &offset);
      }

      pokey_baseMultiplier = read_uint32_from_buffer(buffer, &offset);
   }

   if(cartridge_type == CARTRIDGE_TYPE_SUPERCART_RAM)
   {
      for(index = 0; index < 16384; index++)
         memory_ram[16384 + index] = buffer[offset + index];
      offset += 16384; 
   }
   else if(cartridge_type == CARTRIDGE_TYPE_SOUPER)
   {
      cartridge_souper_chr_bank[0] = buffer[offset++];
      cartridge_souper_chr_bank[1] = buffer[offset++];
      cartridge_souper_mode = buffer[offset++];
      cartridge_souper_ram_page_bank[0] = buffer[offset++];
      cartridge_souper_ram_page_bank[1] = buffer[offset++];
      for(index = 0; index < sizeof(memory_souper_ram); index++)
         memory_souper_ram[index] = buffer[offset++];
      bupchip_flags = buffer[offset++];
      bupchip_volume = buffer[offset++];
      bupchip_current_song = buffer[offset++];
      bupchip_StateLoaded();
   }

   return true;
}

#endif

// ----------------------------------------------------------------------------
// Close
// ----------------------------------------------------------------------------
void prosystem_Close()
{
    cartridge_Release();
    maria_Reset();
    maria_Clear();
    memory_Reset();
    tia_Reset();
    tia_Clear();
}
