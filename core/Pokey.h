// ----------------------------------------------------------------------------
//   ___  ___  ___  ___       ___  ____  ___  _  _
//  /__/ /__/ /  / /__  /__/ /__    /   /_   / |/ /
// /    / \  /__/ ___/ ___/ ___/   /   /__  /    /  emulator
//
// ----------------------------------------------------------------------------
// Copyright 2005 Greg Stanton
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
// PokeySound is Copyright(c) 1997 by Ron Fries
//                                                                           
// This library is free software; you can redistribute it and/or modify it   
// under the terms of version 2 of the GNU Library General Public License    
// as published by the Free Software Foundation.                             
//                                                                           
// This library is distributed in the hope that it will be useful, but       
// WITHOUT ANY WARRANTY; without even the implied warranty of                
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library 
// General Public License for more details.                                  
// To obtain a copy of the GNU Library General Public License, write to the  
// Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   
//                                                                           
// Any permitted reproduction of these routines, in whole or in part, must   
// bear this legend.                                                         
// ----------------------------------------------------------------------------
// Pokey.h
// ----------------------------------------------------------------------------
#ifndef POKEY_H
#define POKEY_H
#define POKEY_AUDF1 0x4000
#define POKEY_AUDC1 0x4001
#define POKEY_AUDF2 0x4002
#define POKEY_AUDC2 0x4003
#define POKEY_AUDF3 0x4004
#define POKEY_AUDC3 0x4005
#define POKEY_AUDF4 0x4006
#define POKEY_AUDC4 0x4007
#define POKEY_AUDCTL 0x4008
#define POKEY_STIMER 0x4009
#define POKEY_SKRES 0x400a
#define POKEY_POTGO 0x400b
#define POKEY_SEROUT 0x400d
#define POKEY_IRQEN 0x400e
#define POKEY_SKCTLS 0x400f

#define POKEY_POT0 0x4000
#define POKEY_POT1 0x4001
#define POKEY_POT2 0x4002
#define POKEY_POT3 0x4003
#define POKEY_POT4 0x4004
#define POKEY_POT5 0x4005
#define POKEY_POT6 0x4006
#define POKEY_POT7 0x4007
#define POKEY_ALLPOT 0x4008
#define POKEY_KBCODE 0x4009
#define POKEY_RANDOM 0x400a
#define POKEY_SERIN 0x400d
#define POKEY_IRQST 0x400e
#define POKEY_SKSTAT 0x400f

#define POKEY_NOTPOLY5 0x80
#define POKEY_POLY4 0x40
#define POKEY_PURE 0x20
#define POKEY_VOLUME_ONLY 0x10
#define POKEY_VOLUME_MASK 0x0f
#define POKEY_POLY9 0x80 
#define POKEY_CH1_179 0x40
#define POKEY_CH3_179 0x20
#define POKEY_CH1_CH2 0x10
#define POKEY_CH3_CH4 0x08
#define POKEY_CH1_FILTER 0x04
#define POKEY_CH2_FILTER 0x02
#define POKEY_CLOCK_15 0x01
#define POKEY_DIV_64 28
#define POKEY_DIV_15 114
#define POKEY_POLY4_SIZE 0x000f
#define POKEY_POLY5_SIZE 0x001f
#define POKEY_POLY9_SIZE 0x01ff
#define POKEY_POLY17_SIZE 0x0001ffff
#define POKEY_CHANNEL1 0
#define POKEY_CHANNEL2 1
#define POKEY_CHANNEL3 2
#define POKEY_CHANNEL4 3
#define POKEY_SAMPLE 4

#include "shared.h"
#define CYCLES_PER_SCANLINE 454

extern void pokey_Reset( );
extern void pokey_SetRegister(word address, byte value);
extern byte pokey_GetRegister(word address);
extern void pokey_Process(void);
extern u16 pokey_ProcessNow(void);
extern void pokey_Clear( );
extern uint32 random_scanline_counter;

/* These are needed for serialization of states. */
extern byte pokey_output[4];
extern byte pokey_outVol[4];
extern byte pokey_poly17[POKEY_POLY17_SIZE];
extern uint pokey_poly17Size;
extern uint pokey_polyAdjust;
extern uint pokey_poly04Cntr;
extern uint pokey_poly05Cntr;
extern uint pokey_poly17Cntr;
extern uint pokey_divideMax[4];
extern uint pokey_divideCount[4];
extern uint pokey_sampleMax;
extern uint pokey_sampleCount[2];
extern uint pokey_baseMultiplier;

extern uint pokey_audf[4];
extern uint pokey_audc[4];
extern uint pokey_audctl;

/* Called prior to each scanline */
inline void pokey_Scanline() 
{
  random_scanline_counter += CYCLES_PER_SCANLINE; 
}


#endif
