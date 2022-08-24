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
// raining too much (???)
#define PkOverThr   (1<<0)
// is raining (???)
#define Raining     (1<<1)
// outern relay is on (???)
#define Out1On      (1<<2)
// heater (???)
#define HtrOn       (1<<3)
// ambient light @0 -> night
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
    uint8_t PeakRS;         // water intensity (255 - continuous) 0..5
    uint8_t SPeakRS;        // 0..5
    uint8_t RainAD8;        // power of rain (???) 64..192
    uint8_t LRA;            // counter of rain activity (???)
    uint8_t TransRat;       // amount of measurements per second (???) 60..170
    uint8_t AmbLNoise;      // ambient noise RMS (???)
    uint8_t RGBits;         // flags
    uint8_t SlowRegIngex;   // slow register index
    uint8_t SlowRegValue;   // slow register value
} rg11;

typedef struct{
    uint8_t RevLevel;     // 12
    uint8_t EmLevel;      // 30..80
    uint8_t RecEmStr;     // 60..66
    uint8_t ABLevel;      // 10
    uint8_t TmprtrF;      // 70..100
    uint8_t PUGain;       // 34..39
    uint8_t ClearTR;      // 60..170
    uint8_t AmbLight;
    uint8_t Bucket;
    uint8_t Barrel;
    uint8_t RGConfig;
    uint8_t DwellT;
    uint8_t SinceRn;
    uint8_t MonoStb;
    uint8_t LightAD;      // 120..136
    uint8_t RainThr;
} slowregs;

int hydreon_open(const char *devname);
void hydreon_close();
int hydreon_getpacket(rg11 *Rregs, slowregs *Sregs);
const char *regname(int N);
const char *rgbitname(int N);
const char *slowname(int N);
