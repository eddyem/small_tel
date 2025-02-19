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
    uint8_t XBits;
    uint8_t YBits;
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

// mount class
typedef struct{
    mcc_errcodes_t  (*init)(conf_t *c); // init device
    void            (*quit)(); // deinit
    mcc_errcodes_t  (*getMountData)(mountdata_t *d); // get last data
    mcc_errcodes_t  (*moveTo)(double X, double Y); // move to given position ans stop
    mcc_errcodes_t  (*emergStop)(); // emergency stop
    mcc_errcodes_t  (*shortCmd)(short_command_t *cmd); // send/get short command
    mcc_errcodes_t  (*longCmd)(long_command_t *cmd); // send/get long command
} mount_t;

extern mount_t Mount;
