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

/*********** base commands ***********/
// get/set X/Y in motsteps
#define CMD_MOTX        "X"
#define CMD_MOTY        "Y"
// set X/Y position with speed "sprintf(buf, "%s%d%s%d", CMD_MOTx, tagx, CMD_MOTxS, tags)
#define CMD_MOTXYS      "S"
// reset current motor position to given value  (and stop, if moving)
#define CMD_MOTXSET     "XF"
#define CMD_MOTYSET     "YF"
// acceleration (per each loop, max: 3900)
#define CMD_MOTXACCEL   "XR"
#define CMD_MOTYACCEL   "YR"
// PID regulator:
// P: 0..32767
#define CMD_PIDPX       "XP"
#define CMD_PIDPY       "YP"
// I: 0..32767
#define CMD_PIDIX       "XI"
#define CMD_PIDIY       "YI"
// limit of I (doesn't work): 0:24000 (WTF???)
#define CMD_PIDILX      "XL"
#define CMD_PIDILY      "YL"
// D: 0..32767
#define CMD_PIDDX       "XD"
#define CMD_PIDDY       "YD"
// current position error
#define CMD_POSERRX     "XE"
#define CMD_POSERRY     "YE"
// max position error limit (X: E#, Y: e#)
#define CMD_POSERRLIMX  "XEL"
#define CMD_POSERRLIMY  "YEL"
// current PWM output: 0..255 (or set max PWM out)
#define CMD_PWMOUTX     "XO"
#define CMD_PWMOUTY     "YO"
// motor current *100 (or set current limit): 0..240
#define CMD_MOTCURNTX   "XC"
#define CMD_MOTCURNTY   "YC"
// change axis to Manual mode and set the PWM output: -255:255
#define CMD_MANUALPWMX  "XM"
#define CMD_MANUALPWMY  "YM"
// change axis to Auto mode
#define CMD_AUTOX       "XA"
#define CMD_AUTOY       "YA"
// get positioin in encoders' ticks or reset it to given value
#define CMD_ENCX        "XZ"
#define CMD_ENCY        "YZ"
// get/set speed (geter x: S#, getter y: s#)
#define CMD_SPEEDX      "XS"
#define CMD_SPEEDY      "YS"
// normal stop X/Y
#define CMD_STOPX       "XN"
#define CMD_STOPY       "YN"
// lower speed -> drag&track or slew&track
#define CMD_STOPTRACKX  "XNT"
#define CMD_STOPTRACKY  "YNT"
// emergency stop
#define CMD_EMSTOPX     "XG"
#define CMD_EMSTOPY     "YG"
// get/set X/Ybits
#define CMD_BITSX       "XB"
#define CMD_BITSY       "YB"

/*********** getters/setters without "Y" variant ***********/
// get handpad status (decimal)
#define CMD_HANDPAD     "XK"
// get TCPU (deg F)
#define CMD_TCPU        "XH"
// get firmware version *10
#define CMD_FIRMVER     "XV"
// get motor voltage *10
#define CMD_MOTVOLTAGE  "XJ"
// get/set current CPU clock (milliseconds)
#define CMD_MILLIS      "XY"
// reset servo
#define CMD_RESET       "XQ"
// clear to factory defaults
#define CMD_CLRDEFAULTS "XU"
// save configuration to flash ROM
#define CMD_WRITEFLASH  "XW"
// read config from flash to RAM
#define CMD_READFLASH   "XT"
// write to flash following full config (128 bytes + 2 bytes of checksum)
#define CMD_PROGFLASH   "FC"
// read configuration (-//-)
#define CMD_DUMPFLASH   "SC"
// get serial number
#define CMD_SERIAL      "YV"

/*********** extended commands ***********/
// get/set latitute
#define CMD_LATITUDE    "XXL"
// getters/setters of motor's encoders per rev
#define CMD_MEPRX       "XXU"
#define CMD_MEPRY       "XXV"
// -//- axis encoders
#define CMD_AEPRX       "XXT"
#define CMD_AEPRY       "XXZ"
// get/set slew rate
#define CMD_SLEWRATEX   "XXA"
#define CMD_SLEWRATEY   "XXB"
// get/set pan rate
#define CMD_PANRATEX    "XXC"
#define CMD_PANRATEY    "XXD"
// get/set platform tracking rate
#define CMD_PLATRATE    "XXE"
// get/set platform up/down adjuster
#define CMD_PLATADJ     "XXF"
// get/set platform goal
#define CMD_PLATGOAL    "XXG"
// get/set guide rate
#define CMD_GUIDERATEX  "XXH"
#define CMD_GUIDERATEY  "XXI"
// get/set picservo timeout (seconds)
#define CMD_PICTMOUT    "XXJ"
// get/set digital outputs of radio handpad
#define CMD_RADIODIGOUT "XXQ"
// get/set argo navis mode
#define CMD_ARGONAVIS   "XXN"
// get/set local search distance
#define CMD_LOSCRCHDISTX    "XXM"
#define CMD_LOSCRCHDISTY    "XXO"
// get/set backlash
#define CMD_BACKLASHX   "XXO"
#define CMD_BACKLASHY   "XXP"


// get binary data of all statistics
#define CMD_GETSTAT     "XXS"
// send short command
#define CMD_SHORTCMD    "XXR"
// send long command
#define CMD_LONGCMD     "YXR"


/*********** special ***********/
// exit ASCII checksum mode
#define CMD_EXITACM     "YXY0\r\xb8"
// controller status:
// X# Y# XZ# YZ# XC# YC# V# T# X[AM] Y[AM] K#
// X,Y - motor, XZ,YZ - encoder, XC,YC - current*100, V - voltage*10, T - temp (F), XA,YA - mode (A[uto]/M[anual]), K - handpad status bits
#define CMD_GETSTATTEXT "\r"

// Loop freq
#define SITECH_LOOP_FREQUENCY   (1953.)

// amount of consequent same coordinates to detect stop
#define MOTOR_STOPPED_CNT       (20)

// steps per revolution (SSI - x4 - for SSI)
// 13312000 / 4 = 3328000
#define X_MOT_STEPSPERREV_SSI   (13312000.)
//#define X_MOT_STEPSPERREV   (3325952.)
#define X_MOT_STEPSPERREV   (3328000.)
// 17578668 / 4 = 4394667
#define Y_MOT_STEPSPERREV_SSI (17578668.)
//#define Y_MOT_STEPSPERREV   (4394960.)
#define Y_MOT_STEPSPERREV   (4394667.)

// convert angle in radians to +-pi
static inline double ang2half(double ang){
    if(ang < -M_PI) ang += 2.*M_PI;
    else if(ang > M_PI) ang -= 2.*M_PI;
    return ang;
}
// convert to only positive: 0..2pi
static inline double ang2full(double ang){
    if(ang < 0.) ang += 2.*M_PI;
    else if(ang > 2.*M_PI) ang -= 2.*M_PI;
    return ang;
}

// motor position to radians and back
#define X_MOT2RAD(n)    ang2half(2. * M_PI * ((double)(n)) / X_MOT_STEPSPERREV)
#define Y_MOT2RAD(n)    ang2half(2. * M_PI * ((double)(n)) / Y_MOT_STEPSPERREV)
#define X_RAD2MOT(r)    ((int32_t)((r) / (2. * M_PI) * X_MOT_STEPSPERREV))
#define Y_RAD2MOT(r)    ((int32_t)((r) / (2. * M_PI) * Y_MOT_STEPSPERREV))
// motor speed in rad/s and back
#define X_MOTSPD2RS(n)  (X_MOT2RAD(n) / 65536. * SITECH_LOOP_FREQUENCY)
#define Y_MOTSPD2RS(n)  (Y_MOT2RAD(n) / 65536. * SITECH_LOOP_FREQUENCY)
#define X_RS2MOTSPD(r)  ((int32_t)(X_RAD2MOT(r) * 65536. / SITECH_LOOP_FREQUENCY))
#define Y_RS2MOTSPD(r)  ((int32_t)(Y_RAD2MOT(r) * 65536. / SITECH_LOOP_FREQUENCY))
// motor acceleration -//-
#define X_MOTACC2RS(n)  (X_MOT2RAD(n) / 65536. * SITECH_LOOP_FREQUENCY * SITECH_LOOP_FREQUENCY)
#define Y_MOTACC2RS(n)  (Y_MOT2RAD(n) / 65536. * SITECH_LOOP_FREQUENCY * SITECH_LOOP_FREQUENCY)
#define X_RS2MOTACC(r)  ((int32_t)(X_RAD2MOT(r) * 65536. / SITECH_LOOP_FREQUENCY / SITECH_LOOP_FREQUENCY))
#define Y_RS2MOTACC(r)  ((int32_t)(Y_RAD2MOT(r) * 65536. / SITECH_LOOP_FREQUENCY / SITECH_LOOP_FREQUENCY))

// adder time to seconds vice versa
#define ADDER2S(a)  ((a) / SITECH_LOOP_FREQUENCY)
#define S2ADDER(s)  ((s) * SITECH_LOOP_FREQUENCY)

// encoder per revolution
#define X_ENC_STEPSPERREV   (67108864.)
#define Y_ENC_STEPSPERREV   (67108864.)
// encoder zero position
#define X_ENC_ZERO          (61245239)
#define Y_ENC_ZERO          (36999830)
// encoder reversed (no: +1)
#define X_ENC_SIGN          (-1.)
#define Y_ENC_SIGN          (-1.)
// encoder position to radians and back
#define X_ENC2RAD(n)    ang2half(X_ENC_SIGN * 2.*M_PI * ((double)(n-X_ENC_ZERO)) / X_ENC_STEPSPERREV)
#define Y_ENC2RAD(n)    ang2half(Y_ENC_SIGN * 2.*M_PI * ((double)(n-Y_ENC_ZERO)) / Y_ENC_STEPSPERREV)
#define X_RAD2ENC(r)    ((uint32_t)((r) / 2./M_PI * X_ENC_STEPSPERREV))
#define Y_RAD2ENC(r)    ((uint32_t)((r) / 2./M_PI * Y_ENC_STEPSPERREV))

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
    xbits_t XBits;      // 18
    ybits_t YBits;      // 19
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

typedef struct{
    uint32_t accel;     // Default Acceleration (0..3900)
    uint32_t backlash;  // Backlash (???)
    uint16_t errlimit;  // Error Limit (0..32767)
    uint16_t propgain;  // Proportional Gain (0..32767)
    uint16_t intgain;   // Integral Gain (0..32767)
    uint16_t derivgain; // Derivative Gain (0..32767)
    uint16_t outplimit; // Output Limit, 0xFF = 100.0 (0..255)
    uint16_t currlimit; // Current Limit * 100 (0..240)
    uint16_t intlimit;  // Integral Limit (0..24000)
} __attribute__((packed)) AxeConfig;

typedef struct{
    AxeConfig Xconf;
    xbits_t xbits;
    uint8_t unused0;
    AxeConfig Yconf;
    ybits_t ybits;
    uint8_t unused1;
    uint8_t address;
    uint8_t unused2;
    uint32_t eqrate;    // Equatorial Rate (platform?) (???)
    int32_t eqadj;      // Equatorial UpDown adjust (???)
    uint32_t trackgoal; // Tracking Platform Goal (???)
    uint16_t latitude;  // Latitude * 100, MSB FIRST!!
    uint32_t Ysetpr;    // Azm Scope Encoder Ticks Per Rev, MSB FIRST!!
    uint32_t Xsetpr;    // Alt Scope Encoder Ticks Per Rev, MSB FIRST!!
    uint32_t Ymetpr;    // Azm Motor Ticks Per Rev, MSB FIRST!!
    uint32_t Xmetpr;    // Alt Motor Ticks Per Rev, MSB FIRST!!
    int32_t Xslewrate;  // Alt/Dec Slew Rate (rates are negative in "through the pole" mode!!!)
    int32_t Yslewrate;  // Azm/RA Slew Rate
    int32_t Xpanrate;   // Alt/Dec Pan Rate
    int32_t Ypanrate;   // Azm/RA Pan Rate
    int32_t Xguiderate; // Alt/Dec Guide Rate
    int32_t Yguiderate; // Azm/RA Guide Rate
    uint8_t unknown0;   // R/A PEC Auto Sync Enable (if low bit = 1), or PicServo Comm Timeout??
    uint8_t unused3;
    uint8_t baudrate;   // Baud Rate??
    uint8_t unused4;
    uint8_t specmode;   // 1 = Enable Argo Navis, 2 = Enable Sky Commander
    uint8_t unused5;
    uint32_t locsdeg;   // Local Search Degrees * 100
    uint32_t locsspeed; // Local Search Speed, arcsec per sec (???)
    uint32_t backlspd;  // Backlash speed
    uint32_t pecticks;  // RA/Azm PEC Ticks
    uint16_t unused6;
    uint16_t checksum;
} __attribute__((packed)) SSconfig;

uint16_t SScalcChecksum(uint8_t *buf, int len);
void SSconvstat(const SSstat *status, mountdata_t *mountdata, double t);
int SStextcmd(const char *cmd, data_t *answer);
int SSrawcmd(const char *cmd, data_t *answer);
int SSgetint(const char *cmd, int64_t *ans);
int SSsetterI(const char *cmd, int32_t ival);
int SSstop(int emerg);
int SSshortCmd(SSscmd *cmd);
mcc_errcodes_t updateMotorPos();
