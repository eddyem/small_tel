/*
 * This file is part of the libsidservo project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

// error codes
typedef enum{
    MCC_E_OK = 0,       // all OK
    MCC_E_FATAL,        // some fatal error
    MCC_E_BADFORMAT,    // wrong arguments of function
    MCC_E_ENCODERDEV,   // encoder device error or can't open
    MCC_E_MOUNTDEV,     // mount device error or can't open
    MCC_E_FAILED,       // failed to run command - protocol error
} mcc_errcodes_t;

typedef struct{
    char*   MountDevPath;       // path to mount device
    int     MountDevSpeed;      // serial speed
    char*   EncoderDevPath;     // path to encoder device
    int     EncoderDevSpeed;    // serial speed
    int     SepEncoder;         // ==1 if encoder works as separate serial device
    double  MountReqInterval;   // maximal interval between subsequent mount requests (seconds)
    ;
} conf_t;

// coordinates in degrees: X, Y and time when they were reached
typedef struct{
    double X; double Y; struct timeval msrtime;
} coords_t;

// data to read/write
typedef struct{
    uint8_t *buf;   // data buffer
    size_t len;     // its length
    size_t maxlen;  // maximal buffer size
} data_t;

typedef struct{
    uint8_t motrev :1;      // If 1, the motor encoder is incremented in the opposite direction
    uint8_t motpolarity :1; // If 1, the motor polarity is reversed
    uint8_t encrev :1;      // If 1, the axis encoder is reversed
    uint8_t dragtrack :1;   // If 1, we are in computerless Drag and Track mode
    uint8_t trackplat :1;   // If 1, we are in the tracking platform mode
    uint8_t handpaden :1;   // If 1, hand paddle is enabled
    uint8_t newpad :1;      // If 1, hand paddle is compatible with New hand paddle, which allows slewing in two directions and guiding
    uint8_t guidemode :1;   // If 1, we are in guide mode. The pan rate is added or subtracted from the current tracking rate
} xbits_t;

typedef struct{
    uint8_t motrev :1;      // If 1, the motor encoder is incremented in the opposite direction
    uint8_t motpolarity :1; // If 1, the motor polarity is reversed
    uint8_t encrev :1;      // If 1, the axis encoder is reversed
    /* If 1, we are in computerless Slew and Track mode
       (no clutches; use handpad to slew; must be in Drag and Track mode too) */
    uint8_t slewtrack :1;
    uint8_t digin_sens :1; // Digital input from radio handpad receiver, or RA PEC Sensor sync
    uint8_t digin :3; // Digital input from radio handpad receiver
} ybits_t;

typedef struct{
    xbits_t XBits;
    ybits_t YBits;
    uint8_t ExtraBits;
    uint16_t ain0;
    uint16_t ain1;
} extradata_t;

typedef struct{
    coords_t motposition;
    coords_t encposition;
    coords_t lastmotposition;
    uint8_t keypad;
    extradata_t extradata;
    uint32_t millis;
    double temperature;
    double voltage;
    int32_t XmotRaw;
    int32_t YmotRaw;
    int32_t XencRaw;
    int32_t YencRaw;
} mountdata_t;

typedef struct{
    ;
} mountstat_t;

typedef struct{
    double Xmot;        // 0  X motor position (rad)
    double Xspeed;      // 4  X speed (rad/s)
    double Ymot;        // 8
    double Yspeed;      // 12
    uint8_t xychange;   // 16 change Xbits/Ybits value
    uint8_t XBits;      // 17
    uint8_t YBits;      // 18
} short_command_t; // short command

typedef struct{
    double Xmot;        // 0  X motor position (rad)
    double Xspeed;      // 4  X speed (rad/s)
    double Ymot;        // 8
    double Yspeed;      // 12
    double Xadder;      // 16 - X adder (rad/s)
    double Yadder;      // 20
    double Xatime;      // 24 X adder time, sec
    double Yatime;      // 28
} long_command_t; // long command

// hardware axe configuration
typedef struct{
    double accel;       // Default Acceleration, rad/s^2
    double backlash;    // Backlash (???)
    double errlimit;    // Error Limit, rad
    double propgain;    // Proportional Gain (???)
    double intgain;     // Integral Gain (???)
    double derivgain;   // Derivative Gain (???)
    double outplimit;   // Output Limit, percent (0..100)
    double currlimit;   // Current Limit (A)
    double intlimit;    // Integral Limit (???)
} __attribute__((packed)) axe_config_t;

// hardware configuration
typedef struct{
    axe_config_t Xconf;
    xbits_t xbits;
    axe_config_t Yconf;
    ybits_t ybits;
    uint8_t address;
    double eqrate;      // Equatorial Rate (???)
    double eqadj;       // Equatorial UpDown adjust (???)
    double trackgoal;   // Tracking Platform Goal (???)
    double latitude;    // Latitude, rad
    uint32_t Ysetpr;    // Azm Scope Encoder Ticks Per Rev
    uint32_t Xsetpr;    // Alt Scope Encoder Ticks Per Rev
    uint32_t Ymetpr;    // Azm Motor Ticks Per Rev
    uint32_t Xmetpr;    // Alt Motor Ticks Per Rev
    double Xslewrate;   // Alt/Dec Slew Rate (rad/s)
    double Yslewrate;   // Azm/RA Slew Rate (rad/s)
    double Xpanrate;    // Alt/Dec Pan Rate (rad/s)
    double Ypanrate;    // Azm/RA Pan Rate (rad/s)
    double Xguiderate;  // Alt/Dec Guide Rate (rad/s)
    double Yguiderate;  // Azm/RA Guide Rate (rad/s)
    uint32_t baudrate;  // Baud Rate (baud)
    double locsdeg;     // Local Search Degrees (rad)
    double locsspeed;   // Local Search Speed (rad/s)
    double backlspd;    // Backlash speed (???)
} hardware_configuration_t;

// mount class
typedef struct{
    mcc_errcodes_t  (*init)(conf_t *c); // init device
    void            (*quit)(); // deinit
    mcc_errcodes_t  (*getMountData)(mountdata_t *d); // get last data
    mcc_errcodes_t  (*moveTo)(const double *X, const double *Y); // move to given position ans stop
    mcc_errcodes_t  (*moveWspeed)(const coords_t *target, const  coords_t *speed); // move with given max speed
    mcc_errcodes_t  (*setSpeed)(const double *X, const double *Y); // set speed
    mcc_errcodes_t  (*stop)(); // stop
    mcc_errcodes_t  (*emergStop)(); // emergency stop
    mcc_errcodes_t  (*shortCmd)(short_command_t *cmd); // send/get short command
    mcc_errcodes_t  (*longCmd)(long_command_t *cmd); // send/get long command
    mcc_errcodes_t  (*getHWconfig)(hardware_configuration_t *c); // get hardware configuration
} mount_t;

extern mount_t Mount;


#ifdef __cplusplus
}
#endif
