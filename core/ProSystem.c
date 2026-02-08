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


/* Rocket - save and load states from original prosystem libretro version were substantially changed.
   This is not implemented beautifully.  Many other cores have state neatly arranged in a single
   struct.  In that case getting the size of the buffer is generally something like sizeof(RAM) + sizeof(state)
   and putting the data into a buffer is also simple.

   This takes bits and pieces of state and packs them in, but I don't want to move a bunch of stuff into 
   structs needlessly adding copies.  So I'm leaving this close to the original prosystem core code.

   This emu emulates a frame of audio and video at a time.  Since the front-end calls these functions it's
   clear these calls would come between calls to retro-run.  Any state which crossed boundaries of frames
   or otherwise persists and can be dynamic as the game is being emulated needs to get saved.

   Some basic logic behind what does NOT need to be saved:
    - If a value/array is set and used "within the frame only" it does not need to be stored.
    - If a value/array is set upon cartridge load and then not changed it does not need to be stored.   
*/

#define PRO_SYSTEM_STATE_HEADER "PRO-SYSTEM-NDS STATE"

bool prosystem_Save(char *buffer)
{
    uint32_t size = 0;
    uint32_t index;
    extern u16 gTotalAtariFrames;
    extern word * framePtr;
    extern uint maria_cycles;

    // This is validated on load to see that the buffer contains a valid savestate for this core.
    for(index = 0; index < 20; index++)
        buffer[size + index] = PRO_SYSTEM_STATE_HEADER[index];  //
    size += 20;

    // This is validated on load to see that we are operating with the same cart loaded.
    for(index = 0; index < 256; index++)
        buffer[size + index] = cartridge_digest[index];
    size += 256;  // Rocket - no longer 32...

 /* Store ALL RAM.  This approach is wasteful for many types 
      of carts.  However, looking at the memory-write code it appears 
      the bank memory is used more or less unconditionally.  So the 
      conservative approach for keeping everything consistent is just
      to store it all.  It makes for fairly big savestate files.  My belief
      for rewind is that the frontend must be keeping deltas of the snapshots
      and not entrire snapshots! At any rate, timing wise we need the worst
      case scenario to work, so this just does the worst case everytime.  */

  // If BIOS is loaded it can be at the very back of RAM.  This catches that too.

    for(index = 0; index < MEMORY_SIZE; index++)
        buffer[size + index] = memory_ram[index];
    size += MEMORY_SIZE;

    for(index = 0; index < sizeof(ex_ram_buffer); index++)
        buffer[size + index] = ex_ram_buffer[index];
    size += sizeof(ex_ram_buffer);

    for(index = 0; index < sizeof(banksets_memory); index++)
        buffer[size + index] = banksets_memory[index];
    size += sizeof(banksets_memory);

    // Rocketfan's guess of which bank state variables matter...
    buffer[size++] = last_bank;
    buffer[size++] = last_ex_ram_bank;
    buffer[size++] = ex_ram_bank;
    buffer[size++] = last_ex_ram_bank_df;
    buffer[size++] = ex_ram_bank_df;

    // A memory bookkeeping array.
    for(index = 0; index < 256; index++)
        buffer[size + index] = is_memory_writable[index];
    size += 256; 

    buffer[size++] = bINPTCTRL_locked;

    // Main emulation loop state.
    save_uint16_to_buffer(buffer, &size, gTotalAtariFrames);
    save_uint32_to_buffer(buffer, &size, (uint32)framePtr);
    save_uint16_to_buffer(buffer, &size, maria_cycles);

    // Sally state.
    buffer[size++] = sally_a;
    buffer[size++] = sally_x;
    buffer[size++] = sally_y;
    save_uint32_to_buffer(buffer, &size, sally_p);
    save_uint32_to_buffer(buffer, &size, sally_s);
    buffer[size++] = sally_pc.b.l;
    buffer[size++] = sally_pc.b.h;
    buffer[size++] = sally_address.b.l;
    buffer[size++] = sally_address.b.h;

    // Tia state.
    for(index = 0; index < 2; index++)
    {
        buffer[size++] = tia_volume[index];
        buffer[size++] = tia_counterMax[index];
        buffer[size++] = tia_counter[index];
        buffer[size++] = tia_audc[index];
        buffer[size++] = tia_audf[index];
        buffer[size++] = tia_audv[index];
        buffer[size++] = tia_poly4Cntr[index];
        buffer[size++] = tia_poly5Cntr[index];
        save_uint16_to_buffer(buffer, &size, tia_poly9Cntr[index]);
    }

    // Pokey State.
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

    // Maria related state that gets modified from Memory.c
    buffer[size++] = bg8;
    save_uint32_to_buffer(buffer, &size, bg32);
    save_uint32_to_buffer(buffer, &size, maria_charbase);
 
    // RIOT state.
    buffer[size++] = riot_dra;
    buffer[size++] = riot_drb;
    save_uint32_to_buffer(buffer, &size, riot_and_wsync);
    save_uint32_to_buffer(buffer, &size, riot_timer);
    save_uint32_to_buffer(buffer, &size, riot_intervals);
    save_uint32_to_buffer(buffer, &size, riot_elapsed);
    save_uint32_to_buffer(buffer, &size, riot_currentTime);
    save_uint32_to_buffer(buffer, &size, riot_clocks);
    save_uint32_to_buffer(buffer, &size, riot_shift);   
   
    // fprintf(stdout, "Save - final size: %d\n", size);
    // fflush(stdout);

    return true;
}


bool prosystem_Load(const char *buffer)
{
    uint32_t index;
    unsigned char digest[256] = {0};
    uint32_t offset = 0;
    extern u16 gTotalAtariFrames;
    extern word * framePtr;
    extern uint maria_cycles;

    for(index = 0; index < 20; index++)
    {
        /* Does the buffer contain a valid ProSystem nds save state? */
        if(buffer[offset + index] != PRO_SYSTEM_STATE_HEADER[index])
            return false;
     }
     offset += 20;

     for(index = 0; index < 256; index++)
         digest[index] = buffer[offset + index];

     offset += 256;

     /* Does this match the loaded cartridge digest? */
     if(memcmp(cartridge_digest, digest, 256) != 0)
         return false;

     // Load ALL RAM. 
     for(index = 0; index < MEMORY_SIZE; index++)
         memory_ram[index] = buffer[offset + index];
     offset += MEMORY_SIZE;

     // Also the banks get stored.
     for(index = 0; index < sizeof(ex_ram_buffer); index++)
         ex_ram_buffer[index] = buffer[offset + index];
     offset += sizeof(ex_ram_buffer);

     for(index = 0; index < sizeof(banksets_memory); index++)
         banksets_memory[index] = buffer[offset + index];
     offset += sizeof(banksets_memory);      

     // Rocketfan's guess of which bank state variables matter...
     last_bank = buffer[offset++];
     last_ex_ram_bank = buffer[offset++];
     ex_ram_bank = buffer[offset++];
     last_ex_ram_bank_df = buffer[offset++];
     ex_ram_bank_df = buffer[offset++];

     // A memory bookkeeping array.
     for(index = 0; index < 256; index++)
         is_memory_writable[index] = buffer[offset + index];
     offset += 256; 

     bINPTCTRL_locked = buffer[offset++];

     // Main emulation loop state.
     gTotalAtariFrames = read_uint16_from_buffer(buffer, &offset);
     framePtr = (word *)read_uint32_from_buffer(buffer, &offset);
     maria_cycles = read_uint16_from_buffer(buffer, &offset);

     // Sally state.
     sally_a      = buffer[offset++];
     sally_x      = buffer[offset++];
     sally_y      = buffer[offset++];
     sally_p      = read_uint32_from_buffer(buffer, &offset);
     sally_s      = read_uint32_from_buffer(buffer, &offset);
     sally_pc.b.l = buffer[offset++];
     sally_pc.b.h = buffer[offset++];
     sally_address.b.l = buffer[offset++];
     sally_address.b.h = buffer[offset++];

     // Sound state for tia and pokey.  
     // Sound in this libretro core is emulated frame by frame, but some sound 
     // states certainly crosses frame boundaries and so needs to be saved.

     // Tia state.
     for(index = 0; index < 2; index++)
     {
         tia_volume[index] = buffer[offset++];
         tia_counterMax[index] = buffer[offset++];
         tia_counter[index] = buffer[offset++];
         tia_audc[index] = buffer[offset++];
         tia_audf[index] = buffer[offset++];
         tia_audv[index] = buffer[offset++];
         tia_poly4Cntr[index] = buffer[offset++];
         tia_poly5Cntr[index] = buffer[offset++];
         tia_poly9Cntr[index] = read_uint16_from_buffer(buffer, &offset);
    }

    // Pokey state.
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

    // Maria related state that gets modified from Memory.c
    bg8 = buffer[offset++];
    bg32 = read_uint32_from_buffer(buffer, &offset);
    maria_charbase = read_uint32_from_buffer(buffer, &offset);


    // RIOT state.
    riot_dra = buffer[offset++]; 
    riot_drb = buffer[offset++];
    riot_and_wsync = read_uint32_from_buffer(buffer, &offset);
    riot_timer = read_uint32_from_buffer(buffer, &offset);
    riot_intervals = read_uint32_from_buffer(buffer, &offset);
    riot_elapsed = read_uint32_from_buffer(buffer, &offset);
    riot_currentTime = read_uint32_from_buffer(buffer, &offset);
    riot_clocks = read_uint32_from_buffer(buffer, &offset);
    riot_shift = read_uint32_from_buffer(buffer, &offset);  

    // fprintf(stdout, "Load - final offset: %d\n", offset);
    // fflush(stdout);

    return true;
}

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

// Rocket - utilities from old core for serialization.  I'm unsure why these functions are
//          storing 4 bits per byte?  Maybe there is a reason though, so I'll keep the pattern.
uint32_t read_uint32_from_buffer(const char* buffer, uint32_t* offset)
{
    uint32_t index = *offset;
    *offset += 8;
    return (uint32_t)buffer[index]     << 28 |
           (uint32_t)buffer[index + 1] << 24 |
           (uint32_t)buffer[index + 2] << 20 |
           (uint32_t)buffer[index + 3] << 16 |
           (uint32_t)buffer[index + 4] << 12 |
           (uint32_t)buffer[index + 5] << 8  |
           (uint32_t)buffer[index + 6] << 4  |
           (uint32_t)buffer[index + 7];
}

void save_uint32_to_buffer(char* buffer, uint32_t* size, uint32_t data)
{
   int i;
   uint8_t shiftby = 32;
   uint32_t index = *size;
   *size += 8;
   for (i = 0; i < 8; i++)
      buffer[index++] = (data >> (shiftby -= 4)) & 0xF;
}

// New functions for 16 bit quantities also store 4 bits per byte.

uint16_t read_uint16_from_buffer(const char* buffer, uint32_t* offset)
{
    uint16_t index = *offset;
    *offset += 4;
    return (uint32_t)buffer[index]     << 12 |
           (uint32_t)buffer[index + 1] << 8 |
           (uint32_t)buffer[index + 2] << 4 |
           (uint32_t)buffer[index + 3];
}

void save_uint16_to_buffer(char* buffer, uint32_t* size, uint16_t data)
{
   int i;
   uint8_t shiftby = 16;
   uint32_t index = *size;
   *size += 4;
   for (i = 0; i < 4; i++)
      buffer[index++] = (data >> (shiftby -= 4)) & 0xF;
}
