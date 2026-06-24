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

#pragma once

#include "angles.h"

// mount statuses
typedef enum{
    MNT_S_TRACKING = 0,
    MNT_S_STOPHOM = 1,
    MNT_S_PARKING = 2,
    MNT_S_UNPARKING = 3,
    MNT_S_HOMING = 4,
    MNT_S_PARKED = 5,
    MNT_S_SLEWING = 6,
    MNT_S_STOPPED = 7,
    MNT_S_INHIBITED = 8,
    MNT_S_OUTLIMIT = 9,
    MNT_S_FOLSAT = 10,
    MNT_S_DATINCOSIST = 11,
    MNT_S_ERROR = 12, // my status
    MNT_S_STATAMOUNT = 13 // number of statuses
} mount_status_t;

void set_emulation_mode();

bool mount_setInpHA(double ha);
bool mount_setInpRA(double ra);
bool mount_setInpDec(double dec);
bool mount_setInpA(double A);
bool mount_setInpZ(double Z);
bool mount_setInpMJD(double m);

double mount_getInpCoords(polarCrds_t *c);
double mount_getTagCoords(polarCrds_t *c);
double mount_getInpMJD(double *MJD);
double mount_getInpHor(horizCrds_t *c);


bool mount_set_name(const char *name);
bool mount_set_dev(char *dev, int speed, int timeout);
const char* mount_status_str();
mount_status_t mount_status();
bool mount_connect();
void mount_disconnect();

mount_status_t mount_getcoords(double *ra, double *dec);
bool mount_point(double ra, double dec);
bool mount_pointAZ(double A, double Z);
bool mount_stop();
bool mount_tracking_start();
bool mount_park();

bool mount_setParkAz(double az);
bool mount_setParkZD(double zd);
void mount_getPark(horizCrds_t *c);
