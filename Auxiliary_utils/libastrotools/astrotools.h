/*
 * This file is part of the astrotools project.
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

#include <erfa.h>
#include <erfam.h>
#include <libnova/libnova.h>
#include <sys/time.h>
#include <time.h>

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef ERFA_DR2S
#define ERFA_DR2S 1.3750987083139757010431557155385240879777313391975e4
#endif

typedef struct{
    double utc1; double utc2; // UTC JD, commonly used MJD = utc1+utc2-2400000.5
    double MJD;
    double tai1; double tai2; // TAI JD
    double tt1;  double tt2;  //  TT JD
} at_MJD_t;

// equatorial coordinates & equation of origins (all in radians)
typedef struct{
    double ha;  // hour angle
    double ra;  // right ascencion
    double dec; // declination
    double eo;  // equation of origins
} at_equat_t;

// horizontal coordinates (all in radians)
typedef struct{
    double az; // azimuth, 0 @ south, positive clockwise
    double zd; // zenith distance
} at_horiz_t;

// observational place coordinates and altitude; all angle coordinates are in radians!
typedef struct{
    double longitude;   // longitude
    double latitude;    // latitude
    double altitude;    // altitude, m
} at_place_t;

// place weather data
typedef struct{
    double relhum;  // rel. humidity, 0..1
    double phpa;    // atm. pressure (hectopascales)
    double tdegc;   // temperature, degrC
} at_weather_t;

// DUT/polar almanach data
typedef struct{
    double DUT1;    // UT1-UTC, sec
    double px;      // polar coordinates, arcsec
    double py;
} at_dut_t;

typedef struct{
    char *str;
    size_t maxlen;
    size_t strlen;
} at_string_t;

typedef struct{
    double pmRA;        // proper motion (radians/year)
    double pmDec;
    double parallax;    // (arcsec)
    double radvel;      // radial velocity (km/s, positive if receding)
} at_star_t;

int at_getPlace(at_place_t *p);
int at_setPlace(at_place_t *p);
int at_getWeath(at_weather_t *w);
int at_setWeath(at_weather_t *w);
int at_getDUT(at_dut_t *a);
int at_setDUT(at_dut_t *a);
void at_setEffWvl(double w);

// strings
int at_chkstring(at_string_t *s, size_t minlen);
at_string_t *at_newstring(size_t maxlen);
void at_delstring(at_string_t **s);
const char *at_libversion();

// human readable
int at_radtoHdeg(double r, at_string_t *s);
int at_radtoHtime(double r, at_string_t *s);

// time-date
int at_get_MJDt(struct timeval *tval, at_MJD_t *mjd);
int at_get_MJDu(double UNIXtime, at_MJD_t *mjd);
int at_get_MJDj(double JD, at_MJD_t *mjd);
double at_get_LST(at_MJD_t *mjd);

// coordinates
void at_hor2eq(at_horiz_t *h, at_equat_t *pc, double LST);
void at_eq2hor(at_equat_t *pc, at_horiz_t *h);
double at_getRA(at_equat_t *pc, double LST);
double at_getHA(at_equat_t *pc, double LST);
int at_get_ObsPlace(at_MJD_t *MJD, at_equat_t *p2000, at_equat_t *pnow, at_horiz_t *hnow);
int at_get_ObsPlaceStar(at_MJD_t *MJD, at_equat_t *p2000, at_star_t *star, at_equat_t *pnow, at_horiz_t *hnow);;
int at_get_mean(at_MJD_t *MJD, at_equat_t *pnow, at_equat_t *p2000);
int at_obs2catP(at_MJD_t *MJD, at_equat_t *pnow, at_equat_t *p2000);
int at_obs2catA(at_MJD_t *MJD, at_horiz_t *hnow, at_equat_t *p2000);
