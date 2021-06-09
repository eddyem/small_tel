/*
 *                                                                                                  geany_encoding=koi8-r
 * telescope.h
 *
 * Copyright 2018 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */
#pragma once
#ifndef __TELESCOPE_H__
#define __TELESCOPE_H__

// max time after last coordinates reading
#define COORDS_TOO_OLD_TIME     (5)
// make datetime/pressure/temperature corrections each CORRECTIONS_TIMEDIFF seconds
#define CORRECTIONS_TIMEDIFF    (3600)

#define TELESCOPE_NAME      "'Astrosib-500 (1)'"

// telescope statuses
typedef enum{
    TEL_TRACKING = 0,
    TEL_STOPHOM = 1,
    TEL_PARKING = 2,
    TEL_UNPARKING = 3,
    TEL_HOMING = 4,
    TEL_PARKED = 5,
    TEL_SLEWING = 6,
    TEL_STOPPED = 7,
    TEL_INHIBITED = 8,
    TEL_OUTLIMIT = 9,
    TEL_FOLSAT = 10,
    TEL_DATINCOSIST = 11,
    TEL_MAXSTATUS = 12 // number of statuses
} tel_status;

int connect_telescope(char *dev, char *hdrname);
int point_telescope(double ra, double decl);
int get_telescope_coords(double *ra, double *decl);
void stop_telescope();
void wrhdr();
void *term_thread(void *sockd);

#endif // __TELESCOPE_H__
