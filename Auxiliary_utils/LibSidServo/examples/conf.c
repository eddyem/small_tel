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
    .EncoderDevPath = "/dev/ttyUSB1",
    .EncoderDevSpeed = 153000,
    .MountReqInterval = 0.1,
    .SepEncoder = 1
};

static sl_option_t opts[] = {
    {"MountDevPath",    NEED_ARG,   NULL,   0,  arg_string, APTR(&Config.MountDevPath),     "path to mount device"},
    {"MountDevSpeed",   NEED_ARG,   NULL,   0,  arg_int,    APTR(&Config.MountDevSpeed),    "serial speed of mount device"},
    {"EncoderDevPath",  NEED_ARG,   NULL,   0,  arg_string, APTR(&Config.EncoderDevPath),   "path to encoder device"},
    {"EncoderDevSpeed", NEED_ARG,   NULL,   0,  arg_int,    APTR(&Config.EncoderDevSpeed),  "serial speed of encoder device"},
    {"MountReqInterval",NEED_ARG,   NULL,   0,  arg_double, APTR(&Config.MountReqInterval), "interval of mount requests (not less than 0.05s)"},
    {"SepEncoder",      NO_ARGS,    NULL,   0,  arg_int,    APTR(&Config.SepEncoder),       "encoder is separate device"},
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
