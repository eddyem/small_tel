/*
 * This file is part of the sofa project.
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
#ifndef SOFA_H__
#define SOFA_H__

#include <sofa.h>
#include <sofam.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

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
    double relhum;  // rel. humidity, 0..1
    double php;     // atm. pressure (hectopascales)
    double tc;      // temperature, degrC
} placeWeather;

// DUT/polar almanach data
typedef struct{
    double DUT1;    // UT1-UTC, sec
    double px;      // polar coordinates, arcsec
    double py;
} almDut;

placeData *getPlace();
void setWeath(double P, double T, double H);
int getWeath(placeWeather *w);
int getDUT(almDut *a);
char *radtodeg(double r);
char *radtohrs(double r);
int get_MJDt(struct timeval *tval, sMJD *MJD);
int get_LST(sMJD *mjd, double dUT1, double slong, double *LST);
void hor2eq(horizCrds *h, polarCrds *pc, double sidTime);
void eq2horH(polarCrds *pc, horizCrds *h);
void eq2hor(polarCrds *pc, horizCrds *h, double sidTime);;
int get_ObsPlace(struct timeval *tval, polarCrds *p2000, polarCrds *pnow, horizCrds *hnow);

#endif // SOFA_H__
