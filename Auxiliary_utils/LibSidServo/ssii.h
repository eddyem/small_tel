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

#include <math.h>
#include <stdint.h>

#include "sidservo.h"

#if 0
// ASCII commands
#define U8P(x)   ((uint8_t*)x)
// get binary data of all statistics
#define CMD_GETSTAT     U8P("XXS")
// send short command
#define CMD_SHORTCMD    U8P("XXR")
// send long command
#define CMD_LONGCMD     U8P("YXR")
// get/set X/Y in motsteps
#define CMD_MOTX        U8P("X")
#define CMD_MOTY        U8P("Y")
// -//- in encoders' ticks
#define CMD_ENCX        U8P("XZ")
#define CMD_ENCY        U8P("YZ")
// normal stop X/Y
#define CMD_STOPX       U8P("XN")
#define CMD_STOPY       U8P("YN")
// emergency stop
#define CMD_EMSTOPX     U8P("XG")
#define CMD_EMSTOPY     U8P("YG")
// getters of motor's encoders per rev
#define CMD_GETXMEPR    U8P("XXU")
#define CMD_GETYMEPR    U8P("XXV")
// -//- axis encoders
#define CMD_GETXAEPR    U8P("XXT")
#define CMD_GETYAEPR    U8P("XXZ")
// exit ASCII checksum mode
#define CMD_EXITACM     U8P("YXY0\r\xb8")
#endif

// get binary data of all statistics
#define CMD_GETSTAT     ("XXS")
// send short command
#define CMD_SHORTCMD    ("XXR")
// send long command
#define CMD_LONGCMD     ("YXR")
// get/set X/Y in motsteps
#define CMD_MOTX        ("X")
#define CMD_MOTY        ("Y")
// -//- in encoders' ticks
#define CMD_ENCX        ("XZ")
#define CMD_ENCY        ("YZ")
// normal stop X/Y
#define CMD_STOPX       ("XN")
#define CMD_STOPY       ("YN")
// emergency stop
#define CMD_EMSTOPX     ("XG")
#define CMD_EMSTOPY     ("YG")
// getters of motor's encoders per rev
#define CMD_GETXMEPR    ("XXU")
#define CMD_GETYMEPR    ("XXV")
// -//- axis encoders
#define CMD_GETXAEPR    ("XXT")
#define CMD_GETYAEPR    ("XXZ")
// exit ASCII checksum mode
#define CMD_EXITACM     ("YXY0\r\xb8")

// steps per revolution
//#define X_MOT_STEPSPERREV   (3325440.)
#define X_MOT_STEPSPERREV   (3325952.)
//#define Y_MOT_STEPSPERREV   (4394496.)
#define Y_MOT_STEPSPERREV   (4394960.)

// motor position to radians and back
#define X_MOT2RAD(n)    (2.*M_PI * (double)n / X_MOT_STEPSPERREV)
#define Y_MOT2RAD(n)    (2.*M_PI * (double)n / Y_MOT_STEPSPERREV)
#define X_RAD2MOT(r)    ((int32_t)(r / 2./M_PI * X_MOT_STEPSPERREV))
#define Y_RAD2MOT(r)    ((int32_t)(r / 2./M_PI * Y_MOT_STEPSPERREV))
// motor speed in rad/s and back
#define X_MOTSPD2RS(n)  (X_MOT2RAD(n)/65536.*1953.)
#define X_RS2MOTSPD(r)  ((int32_t)(X_RAD2MOT(r)*65536./1953.))
#define Y_MOTSPD2RS(n)  (Y_MOT2RAD(n)/65536.*1953.)
#define Y_RS2MOTSPD(r)  ((int32_t)(Y_RAD2MOT(r)*65536./1953.))
// adder time to seconds vice versa
#define ADDER2S(a)  (a*1953.)
#define S2ADDER(s)  (s/1953.)

// encoder per revolution
#define X_ENC_STEPSPERREV   (67108864.)
#define Y_ENC_STEPSPERREV   (67108864.)
// encoder position to radians and back
#define X_ENC2RAD(n)    (2.*M_PI * (double)n / X_ENC_STEPSPERREV)
#define Y_ENC2RAD(n)    (2.*M_PI * (double)n / Y_ENC_STEPSPERREV)
#define X_RAD2ENC(r)    ((uint32_t)(r / 2./M_PI * X_ENC_STEPSPERREV))
#define Y_RAD2ENC(r)    ((uint32_t)(r / 2./M_PI * Y_ENC_STEPSPERREV))

// encoder's tolerance (ticks)
#define YencTOL    (25.)
#define XencTOL    (25.)


// all need data in one
typedef struct{ // 41 bytes
    uint8_t ctrlAddr;   // 0  a8 + controller address
    int32_t Xmot;       // 1  Dec/HA motor position
    int32_t Ymot;       // 5
    int32_t Xenc;       // 9  Dec/HA encoder position
    int32_t Yenc;       // 13
    uint8_t keypad;     // 17 keypad status
    uint8_t XBits;      // 18
    uint8_t YBits;      // 19
    uint8_t ExtraBits;  // 20
    uint16_t ain0;      // 21 analog inputs
    uint16_t ain1;      // 23
    uint32_t millis;    // 25 milliseconds clock
    int8_t tF;          // 29 temperature (degF)
    uint8_t voltage;    // 30 input voltage *10 (RA worm phase?)
    uint32_t XLast;     // 31 Alt/Dec motor location at last Alt/Dec scope encoder location change
    uint32_t YLast;     // 35 Az/RA motor location at last Az/RA scope encoder location change
    uint16_t checksum;  // 39 checksum, H inverted
}__attribute__((packed)) SSstat;

typedef struct{
    int32_t Xmot;       // 0  X motor position
    int32_t Xspeed;     // 4  X speed
    int32_t Ymot;       // 8
    int32_t Yspeed;     // 12
    uint8_t xychange;   // 16 change Xbits/Ybits value
    uint8_t XBits;      // 17
    uint8_t YBits;      // 18
    uint16_t checksum;  // 19
} __attribute__((packed)) SSscmd; // short command

typedef struct{
    int32_t Xmot;       // 0  X motor position
    int32_t Xspeed;     // 4  X speed
    int32_t Ymot;       // 8
    int32_t Yspeed;     // 12
    int32_t Xadder;     // 16 - X adder
    int32_t Yadder;     // 20
    int32_t Xatime;     // 24 X adder time (1953 == 1s)
    int32_t Yatime;     // 28
    uint16_t checksum;  // 32
} __attribute__((packed)) SSlcmd; // long command

uint16_t SScalcChecksum(uint8_t *buf, int len);
void SSconvstat(const SSstat *status, mountdata_t *mountdata, struct timeval *tdat);
int SStextcmd(const char *cmd, data_t *answer);
int SSgetint(const char *cmd, int64_t *ans);
int SSXmoveto(double pos);
int SSYmoveto(double pos);
int SSemergStop();
int SSshortCmd(SSscmd *cmd);
