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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usefull_macros.h>

#include "astrotools.h"

#define ERFA(f, ...) do{if(f(__VA_ARGS__)){WARNX("Error in " #f); return FALSE;}}while(0)

/*********** default parameters (could/should be changed by setters) ***********/
// default - BTA
static at_place_t s_pldata = {
    .longitude = 0.7232763200,
    .latitude = 0.7618977414,
    .altitude = 2070.0
};
static pthread_mutex_t s_pldata_m = PTHREAD_MUTEX_INITIALIZER;
// default weather
static at_weather_t s_weather = {
    .relhum = 0.5,
    .phpa = 800.,
    .tdegc = 0.
};
static pthread_mutex_t s_weather_m = PTHREAD_MUTEX_INITIALIZER;
// default DUT1 and polar motion
static at_dut_t s_dut = {
    .DUT1 = -0.01697,
    .px = 0.,
    .py = 0.
};
static pthread_mutex_t s_dut_m = PTHREAD_MUTEX_INITIALIZER;

// eff. wavelength for observed place calculation
static double s_effwavelength = 0.5;

/**
 * @brief at_getPlace - get stored place data
 * @param p (o) - data
 * @return FALSE if !p
 */
int at_getPlace(at_place_t *p){
    if(!p) return FALSE;
    pthread_mutex_lock(&s_pldata_m);
    *p = s_pldata;
    pthread_mutex_unlock(&s_pldata_m);
    return TRUE;
}
/**
 * @brief at_setPlace - change stored place data
 * @param p (i) - new place
 * @return FALSE if !p or p is wrong
 */
int at_setPlace(at_place_t *p){
    if(!p) return FALSE;
    if(p->longitude > ERFA_DPI || p->longitude < -ERFA_DPI
       || p->latitude > ERFA_DPI/2 || p->latitude < -ERFA_DPI/2) return FALSE;
    pthread_mutex_lock(&s_pldata_m);
    s_pldata = *p;
    pthread_mutex_unlock(&s_pldata_m);
    return TRUE;
}

/**
 * @brief at_getWeath - get last stored weather data
 * @param w (o) - weather
 * @return FALSE if !w
 */
int at_getWeath(at_weather_t *w){
    if(!w) return FALSE;
    pthread_mutex_lock(&s_weather_m);
    *w = s_weather;
    pthread_mutex_unlock(&s_weather_m);
    return TRUE;
}
/**
 * @brief at_setWeath - set new weather data
 * @param w (i) - new weather
 * @return FALSE if !w or w is wrong
 */
int at_setWeath(at_weather_t *w){
    if(!w) return FALSE;
    if(w->phpa < 0 || w->phpa > 2000 ||
       w->relhum < 0. || w->relhum > 1. ||
       w->tdegc < -273.15 || w->tdegc > 100.) return FALSE;
    pthread_mutex_lock(&s_weather_m);
    s_weather = *w;
    pthread_mutex_unlock(&s_weather_m);
    return TRUE;
}

/**
 * @brief at_getDUT - get last stored DUT1 and polar motion data
 * @param a (o) - data
 * @return FALSE if !a
 */
int at_getDUT(at_dut_t *a){
    if(!a) return FALSE;
    pthread_mutex_lock(&s_dut_m);
    *a = s_dut;
    pthread_mutex_unlock(&s_dut_m);
    return TRUE;
}
/**
 * @brief at_setDUT - set new DUT1 and PM
 * @param a (i) - data
 * @return FALSE if !a or a is wrong
 */
int at_setDUT(at_dut_t *a){
    if(!a) return FALSE;
    if(a->DUT1 < -1. || a->DUT1 > 1. ||
       a->px < -1000. || a->px > 1000. ||
       a->py < -1000. || a->py > 1000.) return FALSE;
    pthread_mutex_lock(&s_dut_m);
    s_dut = *a;
    pthread_mutex_unlock(&s_dut_m);
    return TRUE;
}

/**
 * @brief at_chkstring - check if string is large enough
 * @param s - string
 * @param minlen - minimal length (or 0 if not need to check)
 * @return TRUE if all OK
 */
int at_chkstring(at_string_t *s, size_t minlen){
    if(!s || !s->str) return FALSE;
    if(s->maxlen < minlen) s->str = realloc(s->str, minlen+1);
    return TRUE;
}
/**
 * @brief at_newstring
 * @param maxlen
 * @return
 */
at_string_t *at_newstring(size_t maxlen){
    at_string_t *s = MALLOC(at_string_t, 1);
    s->str = MALLOC(char, maxlen+1);
    s->maxlen = maxlen;
    return s;
}
void at_delstring(at_string_t **s){
    if(!s || !*s) return;
    FREE((*s)->str);
    FREE(*s);
}
const char *at_libversion(){
    const char *v = PACKAGE_VERSION;
    return v;
}

/**
 * @brief at_radtoHdeg - reformat angle in radians to human readable string
 * @param r - angle
 * @param s - string
 * @return
 */
int at_radtoHdeg(double r, at_string_t *s){
#define FMTSTRLEN   13
    if(!at_chkstring(s, FMTSTRLEN)) return FALSE;
    int i[4]; char pm;
    r = eraAnpm(r);
    eraA2af(2, r, &pm, i);
    snprintf(s->str, FMTSTRLEN+1, "%c%03d:%02d:%02d.%02d", pm, i[0],i[1],i[2],i[3]);
    s->strlen = FMTSTRLEN;
#undef FMTSTRLEN
    return TRUE;
}
/**
 * @brief at_radtoHtime - reformat time from radians into human readable string
 * @param r - time / hour angle / etc.
 * @param s - string
 * @return
 */
int at_radtoHtime(double r, at_string_t *s){
#define FMTSTRLEN   11
    if(!at_chkstring(s, FMTSTRLEN)) return FALSE;
    int i[4]; char pm;
    r = eraAnp(r);
    eraA2tf(2, r, &pm, i);
    snprintf(s->str, FMTSTRLEN+1, "%02d:%02d:%02d.%02d", i[0],i[1],i[2],i[3]);
    s->strlen = FMTSTRLEN;
#undef FMTSTRLEN
    return TRUE;
}

/**
 * @brief at_get_MJDt - calculate MJD of `struct timeval` from argument
 * @param tval (i) - given date (or NULL for current)
 * @param MJD  (o) - time (or NULL just to check)
 * @return TRUE if all OK
 */
int at_get_MJDt(struct timeval *tval, at_MJD_t *mjd){
    struct tm tms;
    double tSeconds;
    if(!tval){
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        gmtime_r(&currentTime.tv_sec, &tms);
        tSeconds = tms.tm_sec + ((double)currentTime.tv_usec)/1e6;
    }else{
        gmtime_r(&tval->tv_sec, &tms);
        tSeconds = tms.tm_sec + ((double)tval->tv_usec)/1e6;
    }
    int y, m, d;
    y = 1900 + tms.tm_year;
    m = tms.tm_mon + 1;
    d = tms.tm_mday;
    double utc1, utc2;
    /* UTC date. */
    ERFA(eraDtf2d, "UTC", y, m, d, tms.tm_hour, tms.tm_min, tSeconds, &utc1, &utc2);
    at_MJD_t MJD;
    MJD.MJD = utc1 - ERFA_DJM0 + utc2;
    MJD.utc1 = utc1;
    MJD.utc2 = utc2;
    ERFA(eraUtctai, utc1, utc2, &MJD.tai1, &MJD.tai2);
    ERFA(eraTaitt, MJD.tai1, MJD.tai2, &MJD.tt1, &MJD.tt2);
    if(mjd) *mjd = MJD;
    return TRUE;
}
// calculate by UNIX-time
int at_get_MJDu(double UNIXtime, at_MJD_t *mjd){
    struct timeval t;
    t.tv_sec = (time_t) UNIXtime;
    t.tv_usec = (suseconds_t) ((UNIXtime-t.tv_sec) * 1e6);
    return at_get_MJDt(&t, mjd);
}
// get full date by JulianDate
int at_get_MJDj(double JD, at_MJD_t *mjd){
    int y, m, d, H, M;
    double fd, utc1, utc2;
    ERFA(eraJd2cal, JD, 0., &y, &m, &d, &fd);
    fd *= 24.;
    H = (int)fd;
    fd = (fd - H)*60.;
    M = (int)fd;
    fd = (fd - M)*60.;
    ERFA(eraDtf2d, "UTC", y, m, d, H, M, fd, &utc1, &utc2);
    at_MJD_t MJD;
    MJD.MJD = utc1 - ERFA_DJM0 + utc2;
    MJD.utc1 = utc1;
    MJD.utc2 = utc2;
    ERFA(eraUtctai, utc1, utc2, &MJD.tai1, &MJD.tai2);
    ERFA(eraTaitt, MJD.tai1, MJD.tai2, &MJD.tt1, &MJD.tt2);
    if(mjd) *mjd = MJD;
    return TRUE;
}

/**
 * @brief at_get_LST - calculate local siderial time for current dut and place (change them if need with setters)
 * @param mjd (i) - date/time for LST (utc1 & tt used)
 * @param dUT1    - (UT1-UTC)
 * @param longitude   - site longitude (radians)
 * @param LST (o) - local sidereal time (radians)
 * @return lst (radians) or -1 in case of error
 */
double at_get_LST(at_MJD_t *mjd){
    double ut11, ut12;
    if(eraUtcut1(mjd->utc1, mjd->utc2, s_dut.DUT1, &ut11, &ut12)){
        WARNX("error in eraUtcut1");
        return -1.;
    }
    double ST = eraGst06a(ut11, ut12, mjd->tt1, mjd->tt2);
    ST += s_pldata.longitude;
    if(ST > ERFA_D2PI) ST -= ERFA_D2PI;
    return ST;
}

/**
 * @brief at_hor2eq  - convert horizontal coordinates to polar
 * @param h (i)   - horizontal coordinates
 * @param pc (o)  - polar coordinates
 * @param LST - local sidereal time
 */
void at_hor2eq(at_horiz_t *h, at_equat_t *pc, double LST){
    if(!h || !pc) return;
    eraAe2hd(h->az, ERFA_DPI/2. - h->zd, s_pldata.latitude, &pc->ha, &pc->dec); // A,H -> HA,DEC; phi - site latitude
    pc->eo = 0.;
    if(LST >= 0. && LST < ERFA_D2PI) at_getRA(pc, LST);
}

/**
 * @brief at_eq2hor - convert polar coordinates to horizontal
 * @param pc (i)  - polar coordinates (only HA used)
 * @param h (o)   - horizontal coordinates
 * @param sidTime - sidereal time
 */
void at_eq2hor(at_equat_t *pc, at_horiz_t *h){
    if(!h || !pc) return;
    double alt;
    eraHd2ae(pc->ha, pc->dec, s_pldata.latitude, &h->az, &alt);
    h->zd = ERFA_DPI/2. - alt;
}

/**
 * @brief at_getRA - get RA from HA and LST (and change pc->ra)
 * @param pc - polar coordinates
 * @param LST - local sidereal time
 * @return right ascension (radians)
 * WARNING: this function doesn't check arguments (e.g. for 0 < LST < 2pi)
 */
double at_getRA(at_equat_t *pc, double LST){
    double ra = eraAnp(LST - pc->ha + pc->eo);
    pc->ra = ra;
    return ra;
}
/**
 * @brief at_getHA - get HA from RA and LST (and change pc->ha)
 * @param pc - polar coordinates
 * @param LST - local sidereal time
 * @return hour angle (radians)
 * WARNING: this function doesn't check arguments (e.g. for 0 < LST < 2pi)
 */
double at_getHA(at_equat_t *pc, double LST){
    double ha = eraAnpm(LST - pc->ra + pc->eo);
    pc->ha = ha;
    return ha;
}

/**
 * @brief at_setEffWvl - set effective wavelength for el_get_ObsPlace
 * @param w - wavelength in um
 * WITHOUT CHECKING!
 */
void at_setEffWvl(double w){
    s_effwavelength = w;
}

/**
 * @brief at_get_ObsPlaceStar - calculate observed place for given star @MJD
 * @param MJD (i) - date
 * @param p2000 (i) - catalog coordinates
 * @param star (i) - star with proper motion etc.
 * @param pnow (o) - observed place in polar coordinates
 * @param hnow (o) - observed AZ
 * @return FALSE if failed
 */
int at_get_ObsPlaceStar(at_MJD_t *MJD, at_equat_t *p2000, at_star_t *star, at_equat_t *pnow, at_horiz_t *hnow){
    if(!MJD || !p2000 || !star) return -1;
    double aob, zob, hob, dob, rob, eo;
    /*DBG("p2000->ra=%g, p2000->dec=%g,"
        "star->pmRA=%g, star->pmDec=%g, star->parallax=%g, star->radvel=%g,"
        "MJD->utc1=%g, MJD->utc2=%g,"
        "s_dut.DUT1=%g,"
        "s_pldata.longitude=%g, s_pldata.latitude=%g, s_pldata.altitude=%g,"
        "s_dut.px=%g, s_dut.py=%g,"
        "s_weather.phpa=%g, s_weather.tdegc=%g, s_weather.relhum=%g,"
        "s_effwavelength=%g", p2000->ra, p2000->dec,
        star->pmRA, star->pmDec, star->parallax, star->radvel,
        MJD->utc1, MJD->utc2,
        s_dut.DUT1,
        s_pldata.longitude, s_pldata.latitude, s_pldata.altitude,
        s_dut.px, s_dut.py,
        s_weather.phpa, s_weather.tdegc, s_weather.relhum,
        s_effwavelength);*/
    ERFA(eraAtco13,p2000->ra, p2000->dec,
                 star->pmRA, star->pmDec, star->parallax, star->radvel,
                 MJD->utc1, MJD->utc2,
                 s_dut.DUT1,
                 s_pldata.longitude, s_pldata.latitude, s_pldata.altitude,
                 s_dut.px, s_dut.py,
                 s_weather.phpa, s_weather.tdegc, s_weather.relhum,
                 s_effwavelength,
                 &aob, &zob,
                 &hob, &dob, &rob, &eo);
    if(pnow){
        pnow->eo  = eo;
        pnow->ha  = hob;
        pnow->ra  = rob;
        pnow->dec = dob;
    }
    if(hnow){
        hnow->az = aob;
        hnow->zd = zob;
    }
    return TRUE;
}

/**
 * @brief at_get_ObsPlace - calculate observed place (without PM etc) for given date
 * @param tval  (i) - time
 * @param p2000 (i) - polar coordinates for J2000 (only ra/dec used), ICRS (catalog)
 * @param pnow  (o) - polar coordinates for given epoch (or NULL)
 * @param hnow  (o) - horizontal coordinates for given epoch (or NULL)
 * @return FALSE if failed
 */
int at_get_ObsPlace(at_MJD_t *MJD, at_equat_t *p2000, at_equat_t *pnow, at_horiz_t *hnow){
    at_star_t star = {0};
    return at_get_ObsPlaceStar(MJD, p2000, &star, pnow, hnow);
}

/**
 * @brief
 * @param
 */
/**
 * @brief at_get_mean - convert apparent coordinates (for MJD) to mean (JD2000)
 * @param MJD (i) - given date
 * @param pnow (i) - polar coordinates for given date
 * @param p2000 (o) - catalog coordinates
 * @return TRUE if OK
 */
int at_get_mean(at_MJD_t *MJD, at_equat_t *pnow, at_equat_t *p2000){
    if(!MJD || !pnow) return FALSE;
    double ra, dec, eo, ri;
    eraAtic13(pnow->ra, pnow->dec, MJD->tt1, MJD->tt2, &ri, &dec, &eo);
    ra = eraAnp(ri + eo);
    if(p2000){
        p2000->ra = ra;
        p2000->dec = dec;
        p2000->eo = eo;
        at_getHA(p2000, 0.);
    }
    return TRUE;
}

/**
 * @brief at_obs2catE - convert observed place in Ra-Dec coordinates into catalog J2000
 * @param MJD (i) - epoch for `pnow`
 * @param pnow (i) - coordinates
 * @param p2000 (o) - catalog coordinates
 * @return TRUE if OK
 */
int at_obs2catP(at_MJD_t *MJD, at_equat_t *pnow, at_equat_t *p2000){
    double ra, dec;
    ERFA(eraAtoc13, "R", pnow->ra, pnow->dec, MJD->utc1, MJD->utc2,
                    s_dut.DUT1, s_pldata.longitude, s_pldata.latitude, s_pldata.altitude,
                    s_dut.px, s_dut.py,
                    s_weather.phpa, s_weather.tdegc, s_weather.relhum,
                    s_effwavelength,
                    &ra, &dec);
    if(p2000){
        p2000->ra = ra;
        p2000->dec = dec;
        p2000->eo = 0.;
        at_getHA(p2000, 0.);
    }
    return TRUE;
}

/**
 * @brief at_obs2catAconvert - observed place in A-Z coordinates into catalog J2000
 * @param MJD (i) - epoch for `hnow`
 * @param hnow (i) - coordinates
 * @param p2000 (o) - catalog coordinates
 * @return TRUE if OK
 */
int at_obs2catA(at_MJD_t *MJD, at_horiz_t *hnow, at_equat_t *p2000){
    double ra, dec;
    ERFA(eraAtoc13, "A", hnow->az, hnow->zd, MJD->utc1, MJD->utc2,
                    s_dut.DUT1, s_pldata.longitude, s_pldata.latitude, s_pldata.altitude,
                    s_dut.px, s_dut.py,
                    s_weather.phpa, s_weather.tdegc, s_weather.relhum,
                    s_effwavelength,
                    &ra, &dec);
    if(p2000){
        p2000->ra = ra;
        p2000->dec = dec;
        p2000->eo = 0.;
        at_getHA(p2000, 0.);
    }
    return TRUE;
}
