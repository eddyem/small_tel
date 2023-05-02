/*
 * This file is part of the StelD project.
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
#ifndef LIBSOFA_H__
#define LIBSOFA_H__

#include <erfa.h>
#include <erfam.h>
#include <sys/time.h>

// JD2451544.5 == 2000.0
#define MJD2000  (51544)

typedef struct{
    double utc1; double utc2; // UTC JD, commonly used MJD = utc1+utc2-2400000.5
    double MJD;
    double tai1; double tai2; // TAI JD
    double tt1;  double tt2;  //  TT JD
} sMJD;

// polar coordinates & equation of origins (all in radians)
typedef struct{
    double ha;  // hour angle
    double dec; // declination
    double ra;  // right ascension
    double eo;  // equation of origins
} polarCrds;

// horizontal coordinates (all in radians)
typedef struct{
    double az; // azimuth, 0 @ south, positive clockwise
    double zd; // zenith distance
} horizCrds;

// observational place coordinates and altitude; all coordinates are in radians!
typedef struct{
    double slong;   // longitude
    double slat;    // lattitude
    double salt;    // altitude, m
} placeData;

// place weather data
typedef struct{
    double relhum;  // rel. humidity, 0..100%
    double pres;    // atm. pressure (mmHg)
    double tc;      // temperature, degrC
    double rain;    // rain value (0..1)
    double clouds;  // clouds (0 - bad, >2500 - good)
    double wind;    // wind speed, m/s
    double time;    // measurements time
} localWeather;

// DUT/polar almanach data
typedef struct{
    double DUT1;    // UT1-UTC, sec
    double px;      // polar coordinates, arcsec
    double py;
} almDut;

void r2sHMS(double radians, char *hms, int len);
void r2sDMS(double radians, char *hms, int len);
void hor2eq(horizCrds *h, polarCrds *pc, double sidTime);
void eq2horH(polarCrds *pc, horizCrds *h);
void eq2hor(polarCrds *pc, horizCrds *h, double sidTime);
int get_MJDt(struct timeval *tval, sMJD *MJD);
int get_LST(sMJD *mjd, double dUT1, double slong, double *LST);
int get_ObsPlace(struct timeval *tval, polarCrds *p2000, localWeather *weath, polarCrds *pnow, horizCrds *hnow);
almDut *getDUT();
localWeather *getWeath();
placeData *getPlace();
#endif // LIBSOFA_H__
