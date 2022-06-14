/*
 * This file is part of the SSII project.
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
#ifndef SIDSERVO_H__
#define SIDSERVO_H__

#include <usefull_macros.h>

// ASCII commands
#define U8P(x)   ((uint8_t*)x)
// get binary data of all statistics
#define CMD_GETSTAT U8P("XXS")


#define BUFLEN  (256)
// timeout (seconds) of reading answer
#define READTIMEOUT (0.1)

// all need data in one
typedef struct{
    int32_t DECmot;     // Dec/RA motor position
    int32_t RAmot;
    int32_t DECenc;     // Dec/RA encoder position
    int32_t RAenc;
    uint8_t keypad;     // keypad status
    uint8_t XBits;
    uint8_t YBits;
    uint8_t ExtraBits;
    uint16_t ain0;      // analog inputs
    uint16_t ain1;
    uint32_t millis;    // milliseconds clock
    int8_t tF;          // temperature (degF)
    uint8_t voltage;    // input voltage *10
    uint32_t reserved0;
    uint32_t reserved1;
    uint16_t checksum;  // checksum, H inverted
}__attribute__((packed)) SSstat;

int SSinit(char *devpath, int speed);
void SSclose();
int SSwrite(const uint8_t *buf, int len);
uint8_t *SSread(int *l);
int SSgetstat(SSstat *s);

#endif // SIDSERVO_H__
