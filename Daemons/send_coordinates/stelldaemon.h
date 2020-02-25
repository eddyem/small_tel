/*
 * This file is part of the SendCoords project.
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#ifndef STELLDAEMON_H__

#include <stdint.h>

// some statuses
#define _10U_STATUS_TRACKING    (0)
#define _10U_STATUS_SLEWING     (6)

#define DEG2DEC(degr)  ((int32_t)(degr / 90. * ((double)0x40000000)))
#define HRS2RA(hrs)    ((uint32_t)(hrs / 12. * ((double)0x80000000)))
#define DEC2DEG(i32)   (((double)i32)*90./((double)0x40000000))
#define RA2HRS(u32)    (((double)u32)*12. /((double)0x80000000))

typedef struct __attribute__((__packed__)){
    uint16_t len;
    uint16_t type;
    uint64_t time;
    uint32_t ra;
    int32_t dec;
} outdata;

typedef struct __attribute__((__packed__)){
    uint16_t len;
    uint16_t type;
    uint64_t time;
    uint32_t ra;
    int32_t dec;
    int32_t status;
} indata;

void mk_connection();

#define STELLDAEMON_H__
#endif // STELLDAEMON_H__
