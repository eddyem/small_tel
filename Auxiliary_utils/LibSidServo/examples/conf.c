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

#include <stdio.h>
#include <usefull_macros.h>

#include "conf.h"

static conf_t Config = {
    .MountDevPath = "/dev/ttyUSB0",
    .MountDevSpeed = 19200,
    .EncoderXDevPath = "/dev/encoderX0",
    .EncoderYDevPath = "/dev/encoderY0",
    .EncoderDevSpeed = 153000,
    .MountReqInterval = 0.1,
    .EncoderReqInterval = 0.05,
    .SepEncoder = 2,
    .EncoderSpeedInterval = 0.1,
    .XPIDC.P = 0.8,
    .XPIDC.I = 0.1,
    .XPIDC.D = 0.3,
    .XPIDV.P = 1.,
    .XPIDV.I = 0.01,
    .XPIDV.D = 0.2,
    .YPIDC.P = 0.8,
    .YPIDC.I = 0.1,
    .YPIDC.D = 0.3,
    .YPIDV.P = 0.5,
    .YPIDV.I = 0.2,
    .YPIDV.D = 0.5,
};

static sl_option_t opts[] = {
    {"MountDevPath",    NEED_ARG,   NULL,   0,  arg_string, APTR(&Config.MountDevPath),     "path to mount device"},
    {"MountDevSpeed",   NEED_ARG,   NULL,   0,  arg_int,    APTR(&Config.MountDevSpeed),    "serial speed of mount device"},
    {"EncoderDevPath",  NEED_ARG,   NULL,   0,  arg_string, APTR(&Config.EncoderDevPath),   "path to encoder device"},
    {"EncoderDevSpeed", NEED_ARG,   NULL,   0,  arg_int,    APTR(&Config.EncoderDevSpeed),  "serial speed of encoder device"},
    {"MountReqInterval",NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.MountReqInterval), "interval of mount requests (not less than 0.05s)"},
    {"EncoderReqInterval",NEED_ARG, NULL,   0,  arg_double, APTR(&Config.EncoderReqInterval),"interval of encoder requests (in case of sep=2)"},
    {"SepEncoder",      NEED_ARG,   NULL,   0,  arg_int,    APTR(&Config.SepEncoder),       "encoder is separate device (1 - one device, 2 - two devices)"},
    {"EncoderXDevPath", NEED_ARG,   NULL,   0,  arg_string, APTR(&Config.EncoderXDevPath),  "path to X encoder (/dev/encoderX0)"},
    {"EncoderYDevPath", NEED_ARG,   NULL,   0,  arg_string, APTR(&Config.EncoderYDevPath),  "path to Y encoder (/dev/encoderY0)"},
    {"EncoderSpeedInterval", NEED_ARG,NULL, 0,  arg_double, APTR(&Config.EncoderSpeedInterval),"interval of speed calculations, s"},
    {"RunModel",        NEED_ARG,   NULL,   0,  arg_int,    APTR(&Config.RunModel),         "instead of real hardware run emulation"},
    {"XPIDCP",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.XPIDC.P),          "P of X PID (coordinate driven)"},
    {"XPIDCI",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.XPIDC.I),          "I of X PID (coordinate driven)"},
    {"XPIDCD",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.XPIDC.D),          "D of X PID (coordinate driven)"},
    {"YPIDCP",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.YPIDC.P),          "P of Y PID (coordinate driven)"},
    {"YPIDCI",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.YPIDC.I),          "I of Y PID (coordinate driven)"},
    {"YPIDCD",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.YPIDC.D),          "D of Y PID (coordinate driven)"},
    {"XPIDVP",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.XPIDV.P),          "P of X PID (velocity driven)"},
    {"XPIDVI",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.XPIDV.I),          "I of X PID (velocity driven)"},
    {"XPIDVD",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.XPIDV.D),          "D of X PID (velocity driven)"},
    {"YPIDVP",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.YPIDV.P),          "P of Y PID (velocity driven)"},
    {"YPIDVI",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.YPIDV.I),          "I of Y PID (velocity driven)"},
    {"YPIDVD",          NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.YPIDV.D),          "D of Y PID (velocity driven)"},
    end_option
};

conf_t *readServoConf(const char *filename){
    if(!filename) filename = DEFCONFFILE;
    int n = sl_conf_readopts(filename, opts);
    if(n < 0){
        WARNX("Can't read file %s", filename);
        return NULL;
    }
    if(n == 0){
        WARNX("Got ZERO parameters from %s", filename);
        return NULL;
    }
    return &Config;
}

void dumpConf(){
    char *c = sl_print_opts(opts, TRUE);
    printf("Current configuration:\n%s\n", c);
    FREE(c);
}

void confHelp(){
    sl_showhelp(-1, opts);
}
