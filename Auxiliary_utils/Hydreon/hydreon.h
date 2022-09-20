/*
 * This file is part of the Hydreon_RG11 project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <usefull_macros.h>

// amount of datafields
#define RREGNUM     6
#define RGBITNUM    8
#define SREGNUM     16

// RGBits values:
// PeakRS overflow (>255)
#define PkOverThr   (1<<0)
// is raining (after several PKOverThr by fixed time)
#define Raining     (1<<1)
// outern relay is on (after bucket overflows from 18 to 0)
#define Out1On      (1<<2)
// heater is on
#define HtrOn       (1<<3)
// ambient light @0 (murky, twilight)
#define IsDark      (1<<4)
// ???
#define Cndnstn     (1<<5)
// ???
#define Freeze      (1<<6)
// ???
#define Storm       (1<<7)

// minimal packet length (without slow registers)
#define REGMINLEN   (14)
// standard packet length
#define REGLEN      (18)
#define BUFLEN      (32)

typedef struct{
    uint8_t PeakRS;         // water intensity (255 - continuous) 
    uint8_t SPeakRS;        // most time == PeakRS
    uint8_t RainAD8;        // (???) 
    uint8_t LRA;            // average rain activity (~envelope of PeakRS)
    uint8_t TransRat;       // amount of measurements per second (???) 
    uint8_t AmbLNoise;      // ambient noise RMS (???)
    uint8_t RGBits;         // flags
    uint8_t SlowRegIngex;   // slow register index
    uint8_t SlowRegValue;   // slow register value
} rg11;

typedef struct{
    uint8_t RevLevel;     // (??? == 14)
    uint8_t EmLevel;      // (???) seems correlated with RainAD8
    uint8_t RecEmStr;     // (???) seems correlated with RainAD8
    uint8_t ABLevel;      // (??? == 7..12)
    uint8_t TmprtrF;      // (inner T)
    uint8_t PUGain;       // (??? == 37)
    uint8_t ClearTR;      // (??? almost constant == 121..149)
    uint8_t AmbLight;     // ambient light
    uint8_t Bucket;       // Intergal PeakRS. When no rain, decreased near 4 hours per 1 unit
    uint8_t Barrel;       // Integral Bucket (increases when Bucket goes througt 12->14 after last overflow). Decreased near 2 hours per 1 unit
    uint8_t RGConfig;     // (??? == 0)
    uint8_t DwellT;       // 100 - no rain, 50 - low, 5 - max rain (like exponental function)
    uint8_t SinceRn;      // (0..20) increases every minute after rain is over
    uint8_t MonoStb;      // when Raining==1, MonoStb=15, then decrements when no rain (1 unit per ~1minute)
    uint8_t LightAD;      // (???) seems correlated with RainAD8
    uint8_t RainThr;      // (??? == 12)
} slowregs;

int hydreon_open(const char *devname);
void hydreon_close();
int hydreon_getpacket(rg11 *Rregs, slowregs *Sregs);
const char *regname(int N);
const char *rgbitname(int N);
const char *slowname(int N);
