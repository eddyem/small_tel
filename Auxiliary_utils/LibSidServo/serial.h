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

#include "sidservo.h"
#include "ssii.h"

// magick starting sequence
#define ENC_MAGICK  (204)
// encoder data sequence length
#define ENC_DATALEN (13)
// max error counter (when read() returns -1)
#define MAX_ERR_CTR (100)

data_t *cmd2dat(const char *cmd);
void data_free(data_t **x);
int openEncoder();
int openMount();
void closeSerial();
mcc_errcodes_t getMD(mountdata_t  *d);
void setStat(axis_status_t Xstate, axis_status_t Ystate);
int MountWriteRead(const data_t *out, data_t *in);
int MountWriteReadRaw(const data_t *out, data_t *in);
int cmdS(SSscmd *cmd);
int cmdL(SSlcmd *cmd);
int cmdC(SSconfig *conf, int rw);
void getXspeed();
void getYspeed();
