/*
 * This file is part of the mountdaemon_10micron project.
 * Copyright 2026 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <weather_data.h>

#include "angles.h"
#include "mount.h"

// maximal string length (including terminating zero)
#define MAX_HDR_STRLEN  32

// status checking interval and writing FITS-header, seconds
#define MOUNT_CHECK_T       0.5

typedef struct{
    weather_data_t weather;
    mount_status_t status;
    almDut_t dut;
    placeData_t place;
    polarCrds_t polar;
    horizCrds_t altaz;
    sMJD_t MJD;
    double sidtime;
    char pierside[MAX_HDR_STRLEN];
    char mountname[MAX_HDR_STRLEN];
} fitsheader_t;

bool set_header_name(const char *name);
void wrhdr(fitsheader_t *HDR);
