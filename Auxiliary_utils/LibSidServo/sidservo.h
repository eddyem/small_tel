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

/*
 * This file contains all need for external usage
 */


#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

// acceptable position error - 0.1''
#define MCC_POSITION_ERROR      (5e-7)
// acceptable disagreement between motor and axis encoders - 2''
#define MCC_ENCODERS_ERROR      (1e-7)

// max speeds (rad/s): xs=10 deg/s, ys=8 deg/s
#define MCC_MAX_X_SPEED         (0.174533)
#define MCC_MAX_Y_SPEED         (0.139626)
// accelerations by both axis (for model); TODO: move speeds/accelerations into config?
// xa=12.6 deg/s^2, ya= 9.5 deg/s^2
#define MCC_X_ACCELERATION      (0.219911)
#define MCC_Y_ACCELERATION      (0.165806)

// max speed interval, seconds
#define MCC_CONF_MAX_SPEEDINT   (2.)
// minimal speed interval in parts of EncoderReqInterval
#define MCC_CONF_MIN_SPEEDC     (3.)
// PID I cycle time (analog of "RC" for PID on opamps)
#define MCC_PID_CYCLE_TIME      (5.)
// maximal PID refresh time interval (if larger all old data will be cleared)
#define MCC_PID_MAX_DT          (1.)
// normal PID refresh interval
#define MCC_PID_REFRESH_DT      (0.1)
// boundary conditions for axis state: "slewing/pointing/guiding"
// if angle < MCC_MAX_POINTING_ERR, change state from "slewing" to "pointing": 5 degrees
//#define MCC_MAX_POINTING_ERR    (0.20943951)
#define MCC_MAX_POINTING_ERR    (0.08726646)
// if angle < MCC_MAX_GUIDING_ERR, chane state from "pointing" to "guiding": 1.5 deg
#define MCC_MAX_GUIDING_ERR     (0.026179939)
// if error less than this value we suppose that target is captured and guiding is good: 0.1''
#define MCC_MAX_ATTARGET_ERR    (4.8481368e-7)

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
    double P, I, D;
} PIDpar_t;

typedef struct{
    char*   MountDevPath;           // path to mount device
    int     MountDevSpeed;          // serial speed
    char*   EncoderDevPath;         // path to encoder device
    int     EncoderDevSpeed;        // serial speed
    int     SepEncoder;             // ==1 if encoder works as separate serial device, ==2 if there's new version with two devices
    char*   EncoderXDevPath;        // paths to new controller devices
    char*   EncoderYDevPath;
    double  MountReqInterval;       // interval between subsequent mount requests (seconds)
    double  EncoderReqInterval;     // interval between subsequent encoder requests (seconds)
    double  EncoderSpeedInterval;   // interval between speed calculations
    int     RunModel;               // == 1 if you want to use model instead of real mount
    PIDpar_t XPIDC;                 // gain parameters of PID for both axiss (C - coordinate driven, V - velocity driven)
    PIDpar_t XPIDV;
    PIDpar_t YPIDC;
    PIDpar_t YPIDV;
} conf_t;

// coordinates/speeds in degrees or d/s: X, Y
typedef struct{
    double X; double Y;
} coordpair_t;

// coordinate/speed and time of last measurement
typedef struct{
    double val;
    double t;
} coordval_t;

typedef struct{
    coordval_t X;
    coordval_t Y;
} coordval_pair_t;

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
    uint8_t digin_sens :1;  // Digital input from radio handpad receiver, or RA PEC Sensor sync
    uint8_t digin :3;       // Digital input from radio handpad receiver
} ybits_t;

typedef struct{
    xbits_t XBits;
    ybits_t YBits;
    uint8_t ExtraBits;
    uint16_t ain0;
    uint16_t ain1;
} extradata_t;

typedef enum{
    AXIS_STOPPED,
    AXIS_SLEWING,
    AXIS_POINTING,
    AXIS_GUIDING,
    AXIS_ERROR,
} axis_status_t;

typedef struct{
    axis_status_t Xstate;
    axis_status_t Ystate;
    coordval_t motXposition;
    coordval_t motYposition;
    coordval_t encXposition;
    coordval_t encYposition;
    coordval_t encXspeed; // once per <config> s
    coordval_t encYspeed;
    uint8_t keypad;
    extradata_t extradata;
    uint32_t millis;
    double temperature;
    double voltage;
} mountdata_t;

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

// hardware axis configuration
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
} __attribute__((packed)) axis_config_t;

// hardware configuration
typedef struct{
    axis_config_t Xconf;
    xbits_t xbits;
    axis_config_t Yconf;
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
    double backlspd;    // Backlash speed (rad/s)
} hardware_configuration_t;

/* flags for slew function
typedef struct{
    uint32_t slewNguide : 1; // ==1 to guide after slewing
} slewflags_t;
*/
// mount class
typedef struct{
    // TODO: on init/quit clear all XY-bits to default`
    mcc_errcodes_t  (*init)(conf_t *c); // init device
    void            (*quit)(); // deinit
    mcc_errcodes_t  (*getMountData)(mountdata_t *d); // get last data
//    mcc_errcodes_t  (*slewTo)(const coordpair_t *target, slewflags_t flags);
    mcc_errcodes_t  (*correctTo)(const coordval_pair_t *target, const coordpair_t *endpoint);
    mcc_errcodes_t  (*moveTo)(const coordpair_t *target); // move to given position and stop
    mcc_errcodes_t  (*moveWspeed)(const coordpair_t *target, const  coordpair_t *speed); // move with given max speed
    mcc_errcodes_t  (*setSpeed)(const coordpair_t *tagspeed); // set speed
    mcc_errcodes_t  (*stop)(); // stop
    mcc_errcodes_t  (*emergStop)(); // emergency stop
    mcc_errcodes_t  (*shortCmd)(short_command_t *cmd); // send/get short command
    mcc_errcodes_t  (*longCmd)(long_command_t *cmd); // send/get long command
    mcc_errcodes_t  (*getHWconfig)(hardware_configuration_t *c); // get hardware configuration
    mcc_errcodes_t  (*saveHWconfig)(hardware_configuration_t *c); // save hardware configuration
    double          (*currentT)(); // current time
} mount_t;

extern mount_t Mount;

#ifdef __cplusplus
}
#endif
