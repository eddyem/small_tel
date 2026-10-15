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

#include <erfam.h>
#include <weather_data.h>

#define RADEC_STR_MAXLEN    64

#define RAD2DEG(angle)  ((angle) * ERFA_DR2D)
#define DEG2RAD(angle)  ((angle) * ERFA_DD2R)
#define RAD2HRS(angle)  ((angle) * 12. / ERFA_DPI)
#define HRS2RAD(hour)   ((hour) * ERFA_DPI / 12.)

typedef struct{
    double utc1; double utc2; // UTC JD, commonly used MJD = utc1+utc2-2400000.5
    double MJD;
    double tai1; double tai2; // TAI JD
    double tt1;  double tt2;  //  TT JD
} sMJD_t;

// polar coordinates & equation of origins (all in radians)
typedef struct{
    double ha;  // hour angle
    double dec; // declination
    double ra;  // right ascension
} polarCrds_t;

// horizontal coordinates (all in radians)
typedef struct{
    double az; // azimuth, 0 @ south, positive clockwise
    double zd; // zenith distance
} horizCrds_t;

// observational place coordinates and altitude; all coordinates are in radians!
typedef struct{
    double slong;   // longitude, radians
    double slat;    // lattitude, radians
    double salt;    // altitude, m
} placeData_t;

// DUT/polar almanach data
// TODO: add function to read this data from almanach
typedef struct{
    double DUT1;    // UT1-UTC, sec
    double px;      // polar coordinates, arcsec
    double py;
} almDut_t;

#if 0
char *ra2str(double ra, char buf[RADEC_STR_MAXLEN]);
char *dec2str(double dec, char buf[RADEC_STR_MAXLEN]);
#endif
bool str2coord(const char *str, double *val);
bool normAZ(double *a, double *z);
void norm_RA(double *ra);
void norm_RADEC(double *ra, double *dec);
void norm_RADECr(double *ra, double *dec);
void norm_angle180(double *a);

double raha(double hara);

void hor2eq(horizCrds_t *h, polarCrds_t *pc, double sidTime);
void eq2horH(polarCrds_t *pc, horizCrds_t *h);
void eq2hor(polarCrds_t *pc, horizCrds_t *h, double sidTime);

void r2sHMS(double radians, char buf[RADEC_STR_MAXLEN]);
void r2sDMS(double radians, char buf[RADEC_STR_MAXLEN]);
void d2sDMS(double degrees, char buf[RADEC_STR_MAXLEN]);
void h2sHMS(double hours, char buf[RADEC_STR_MAXLEN]);

bool get_MJDt(struct timeval *tval, sMJD_t *MJD);
bool get_LST(sMJD_t *mjd, double *LST);
#if 0
bool get_ObsPlace(struct timeval *tval, polarCrds_t *p2000, polarCrds_t *pnow, horizCrds_t *hnow);
#endif

bool setDUT(almDut_t *D);
void getDUT(almDut_t *D);

bool setPlaceData(placeData_t *pd);
void getPlaceData(placeData_t *pd);

bool J2000_to_epoch_pn(const polarCrds_t *j2000, double epoch, polarCrds_t *jepoch);
bool epoch_to_J2000_pn(const polarCrds_t *jepoch, double epoch, polarCrds_t *j2000);

bool JnowtoJ2000(const polarCrds_t *in, polarCrds_t *out);
bool JXtoJnow(const polarCrds_t *in, polarCrds_t *out, double Jx);
