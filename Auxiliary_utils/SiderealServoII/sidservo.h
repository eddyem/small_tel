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

#include <stdint.h>

// ASCII commands
#define U8P(x)   ((uint8_t*)x)
// get binary data of all statistics
#define CMD_GETSTAT     U8P("XXS")
// send short command
#define CMD_SHORTCMD    U8P("XXR")
// send long command
#define CMD_LONGCMD     U8P("YXR")
// get/set HA/DEC in motsteps
#define CMD_MOTDEC      U8P("X")
#define CMD_MOTHA       U8P("Y")
// -//- in encoders' ticks
#define CMD_ENCDEC      U8P("XZ")
#define CMD_ENCHA       U8P("YZ")
// normal stop dec/ra
#define CMD_STOPDEC     U8P("XN")
#define CMD_STOPHA      U8P("YN")
// emergency stop
#define CMD_EMSTOPDEC   U8P("XG")
#define CMD_EMSTOPHA    U8P("YG")
// getters of motor's encoders per rev
#define CMD_GETDECMEPR  U8P("XXU")
#define CMD_GETHAMEPR   U8P("XXV")
// -//- axis encoders
#define CMD_GETDECAEPR  U8P("XXT")
#define CMD_GETHAAEPR   U8P("XXZ")

#define BUFLEN  (256)
// timeout (seconds) of reading answer (from last symbol read)
#define READTIMEOUT (0.05)

// Zero positions of RA/DEC encoderc
#define HA_ENC_ZEROPOS  (43066232)
#define DEC_ENC_ZEROPOS (37282120)




// all need data in one
typedef struct{ // 41 bytes
    uint8_t ctrlAddr;   // 0  a8 + controller address
    int32_t DECmot;     // 1  Dec/HA motor position
    int32_t HAmot;      // 5
    int32_t DECenc;     // 9  Dec/HA encoder position
    int32_t HAenc;      // 13
    uint8_t keypad;     // 17 keypad status
    uint8_t XBits;      // 18
    uint8_t YBits;      // 19
    uint8_t ExtraBits;  // 20
    uint16_t ain0;      // 21 analog inputs
    uint16_t ain1;      // 23
    uint32_t millis;    // 25 milliseconds clock
    int8_t tF;          // 29 temperature (degF)
    uint8_t voltage;    // 30 input voltage *10 (RA worm phase?)
    uint32_t DecLast;   // 31 Alt/Dec motor location at last Alt/Dec scope encoder location change
    uint32_t HALast;    // 35 Az/RA motor location at last Az/RA scope encoder location change
    uint16_t checksum;  // 39 checksum, H inverted
}__attribute__((packed)) SSstat;

typedef struct{
    int32_t DECmot;     // 0  DEC motor position
    int32_t DECspeed;   // 4  DEC speed
    int32_t HAmot;      // 8
    int32_t HAspeed;    // 12
    uint8_t xychange;   // 16 change Xbits/Ybits value
    uint8_t XBits;      // 17
    uint8_t YBits;      // 18
    uint16_t checksum;  // 19
} __attribute__((packed)) SSscmd; // short command

typedef struct{
    int32_t DECmot;     // 0  DEC motor position
    int32_t DECspeed;   // 4  DEC speed
    int32_t HAmot;      // 8
    int32_t HAspeed;    // 12
    int32_t DECadder;   // 16 - DEC adder
    int32_t HAadder;    // 20
    int32_t DECatime;   // 24 DEC adder time
    int32_t HAatime;    // 28
    uint16_t checksum;  // 32
} __attribute__((packed)) SSlcmd; // long command

int SSinit(char *devpath, int speed);
void SSclose();
int SSwrite(const uint8_t *buf, int len, int isbin);
int SSwritecmd(const uint8_t *cmd);
int SSgetInt(const uint8_t *cmd, int64_t *ans);
int SScmds(SSscmd *cmd);
int SScmdl(SSlcmd *cmd);
uint8_t *SSread(int *l);
int SSgetstat(SSstat *s);
int SSgetPartialstat(SSstat *s);
double SSticks2deg(int32_t ticks, int isdec);
double SSenc2deg(int32_t ticks, int isdec);
int32_t SSdeg2ticks(double d, int isdec);
int32_t SSdeg2enc(double d, int isdec);
int SSgoto(double ha, double dec);
void SSwaitmoving();
int SScatchtarg(double ra, double dec);

#endif // SIDSERVO_H__
