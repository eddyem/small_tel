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

#include <time.h>

#include "libsofa.h"
#include "socket.h"
#include "usefull_macro.h"

#ifdef EBUG
void reprd(char* s, double ra, double dc){
    char pm;
    int i[4];
    printf ( "%s:", s );
    iauA2tf ( 7, ra, &pm, i );
    printf ( " %2.2d %2.2d %2.2d.%7.7d", i[0],i[1],i[2],i[3] );
    iauA2af ( 6, dc, &pm, i );
    printf ( " %c%2.2d %2.2d %2.2d.%6.6d\n", pm, i[0],i[1],i[2],i[3] );
}
void radtodeg(double r){
    int i[4]; char pm;
    int rem = (int)(r / D2PI);
    if(rem) r -= D2PI * rem;
    if(r > DPI) r -= D2PI;
    else if(r < -DPI) r += D2PI;
    iauA2af (2, r, &pm, i);
    printf("%c%02d %02d %02d.%2.d", pm, i[0],i[1],i[2],i[3]);
}
#define REP(a,b,c) reprd(a,b,c)
#else
#define REP(a,b,c)
#endif

// temporal stubs for weather/place/DUT1 data; user can change values of these variables
static placeData place = {.slong = 0.7232763200, .slat = 0.7618977414, .salt = 2070.};
placeData *getPlace(){
    return &place;
}

static localWeather weather = {0};
typedef struct{
    const char *name;
    double *valptr;
} weathpars;
#define WPCOUNT     (7)
static weathpars WPars[WPCOUNT] = {
    {"BTAHumid", &weather.relhum},
    {"BTAPres", &weather.pres},
    {"Exttemp", &weather.tc},
    {"Rain", &weather.rain},
    {"Clouds", &weather.clouds},
    {"Wind", &weather.wind},
    {"Time", &weather.time}
};

localWeather *getWeath(){
    //DBG("DT=%zd", time(NULL) - (time_t)weather.time);
    char *w = getweathbuffer();
    //DBG("w=%s", w);
    if(w){ // get new data - check it
        int ctr = 0;
        for(int i = 0; i < WPCOUNT; ++i){
            if(getparval(WPars[i].name, w, WPars[i].valptr)) ++ctr;
        }
        if(ctr != WPCOUNT) WARN("Not full set of parameters in %s", w);
        FREE(w);
    }
    if((time_t)weather.time == 0 || time(NULL) - (time_t)weather.time > 3600) return NULL;
    return &weather;
}
static almDut dut1 = {0};
almDut *getDUT(){
    // check DUT1 data HERE once per some time
    return &dut1;
}

/**
 * @brief r2sHMS  - convert angle in radians into string "'HH:MM:SS.SS'"
 * @param radians - angle
 * @param hms (o) - string
 * @param len     - length of hms
 */
void r2sHMS(double radians, char *hms, int len){
    char pm;
    int i[4];
    iauA2tf(2, radians, &pm, i);
    snprintf(hms, len, "'%c%02d:%02d:%02d.%02d'", pm, i[0],i[1],i[2],i[3]);
}

/**
 * @brief r2sDMS  - convert angle in radians into string "'DD:MM:SS.S'"
 * @param radians - angle
 * @param dms (o) - string
 * @param len     - length of hms
 */
void r2sDMS(double radians, char *dms, int len){
    char pm;
    int i[4];
    iauA2af(1, radians, &pm, i);
    snprintf(dms, len, "'%c%02d:%02d:%02d.%d'", pm, i[0],i[1],i[2],i[3]);
}

/**
 * @brief get_MJDt - calculate MJD of date from argument
 * @param tval (i) - given date (or NULL for current)
 * @param MJD  (o) - time (or NULL just to check)
 * @return 0 if all OK
 */
int get_MJDt(struct timeval *tval, sMJD *MJD){
    struct tm tms;
    double tSeconds;
    if(!tval){
        //DBG("MJD for current time");
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
    if(iauDtf2d("UTC", y, m, d, tms.tm_hour, tms.tm_min, tSeconds, &utc1, &utc2) < 0) return -1;
    if(!MJD) return 0;
    MJD->MJD = utc1 - 2400000.5 + utc2;
    MJD->utc1 = utc1;
    MJD->utc2 = utc2;
    //DBG("UTC(m): %g, %.8f\n", utc1 - 2400000.5, utc2);
    if(iauUtctai(utc1, utc2, &MJD->tai1, &MJD->tai2)) return -1;
    //DBG("TAI");
    if(iauTaitt(MJD->tai1, MJD->tai2, &MJD->tt1, &MJD->tt2)) return -1;
    //DBG("TT");
    return 0;
}

/**
 * @brief get_LST - calculate local siderial time
 * @param mjd (i) - date/time for LST (utc1 & tt used)
 * @param dUT1    - (UT1-UTC)
 * @param slong   - site longitude (radians)
 * @param LST (o) - local sidereal time (radians)
 * @return 0 if all OK
 */
int get_LST(sMJD *mjd, double dUT1, double slong, double *LST){
    double ut11, ut12;
    sMJD Mjd;
    if(!mjd){
        if(get_MJDt(NULL, &Mjd)) return 1;
    }else memcpy(&Mjd, mjd, sizeof(sMJD));
    if(iauUtcut1(Mjd.utc1, Mjd.utc2, dUT1, &ut11, &ut12)) return 2;
    /*double era = iauEra00(ut11, ut12) + slong;
    double eo = iauEe06a(mjd->tt1, mjd->tt2);
    printf("ERA = %s; ", radtohrs(era));
    printf("ERA-eo = %s\n", radtohrs(era-eo));*/
    if(!LST) return 0;
    double ST = iauGst06a(ut11, ut12, Mjd.tt1, Mjd.tt2);
    ST += slong;
    if(ST > D2PI) ST -= D2PI;
    else if(ST < 0.) ST += D2PI;
    *LST = ST;
    return 0;
}


/**
 * @brief hor2eq  - convert horizontal coordinates to polar
 * @param h (i)   - horizontal coordinates
 * @param pc (o)  - polar coordinates
 * @param sidTime - sidereal time
 */
void hor2eq(horizCrds *h, polarCrds *pc, double sidTime){
    if(!h || !pc) return;
    placeData *p = getPlace();
    iauAe2hd(h->az, DPI/2. - h->zd, p->slat, &pc->ha, &pc->dec); // A,H -> HA,DEC; phi - site latitude
    pc->ra = sidTime - pc->ha;
    pc->eo = 0.;
}

/**
 * @brief eq2horH - convert polar coordinates to horizontal
 * @param pc (i)  - polar coordinates (only HA used)
 * @param h (o)   - horizontal coordinates
 * @param sidTime - sidereal time
 */
void eq2horH(polarCrds *pc, horizCrds *h){
    if(!h || !pc) return;
    placeData *p = getPlace();
    double alt;
    iauHd2ae(pc->ha, pc->dec, p->slat, &h->az, &alt);
    h->zd = DPI/2. - alt;
}

/**
 * @brief eq2hor  - convert polar coordinates to horizontal
 * @param pc (i)  - polar coordinates (only RA used)
 * @param h (o)   - horizontal coordinates
 * @param sidTime - sidereal time
 */
void eq2hor(polarCrds *pc, horizCrds *h, double sidTime){
    if(!h || !pc) return;
    double ha = sidTime - pc->ra + pc->eo;
    placeData *p = getPlace();
    double alt;
    iauHd2ae(ha, pc->dec, p->slat, &h->az, &alt);
    h->zd = DPI/2. - alt;
}


/**
 * @brief get_ObsPlace - calculate observed place (without PM etc) for given date @550nm
 * @param tval  (i) - time
 * @param p2000 (i) - polar coordinates for J2000 (only ra/dec used), ICRS (catalog)
 * @param weath (i) - weather data (relhum, temp, press) or NULL if none
 * @param pnow  (o) - polar coordinates for given epoch (or NULL)
 * @param hnow  (o) - horizontal coordinates for given epoch (or NULL)
 * @return 0 if all OK
 */
int get_ObsPlace(struct timeval *tval, polarCrds *p2000, localWeather *weath, polarCrds *pnow, horizCrds *hnow){
    double pr = 0.0;     // RA proper motion (radians/year; Note 2)
    double pd = 0.0;     // Dec proper motion (radians/year)
    double px = 0.0;     // parallax (arcsec)
    double rv = 0.0;     // radial velocity (km/s, positive if receding)
    sMJD MJD;
    if(get_MJDt(tval, &MJD)) return -1;
    if(!p2000) return -1;
    /* Effective wavelength (microns) */
    double wl = 0.55;
    /* ICRS to observed. */
    double aob, zob, hob, dob, rob, eo;
    double p = 0., t = 0., h = 0.;
    if(weath){
        p = weath->pres; t = weath->tc; h = weath->relhum;
    }
    /*
        DBG("iauAtco13(%g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g)",
            p2000->ra, p2000->dec, pr, pd, px, rv, MJD.utc1, MJD.utc2, d.DUT1, p.slong, p.slat, p.salt,
                             d.px, d.py, p, t, h, wl);
    */
    if(iauAtco13(p2000->ra, p2000->dec,
                 pr, pd, px, rv,
                 MJD.utc1, MJD.utc2,
                 dut1.DUT1,
                 place.slong, place.slat, place.salt,
                 dut1.px, dut1.py,
                 p, t, h,
                 wl,
                 &aob, &zob,
                 &hob, &dob, &rob, &eo)) return -1;
    REP("ICRS->observed",  rob, dob);
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
#ifdef EBUG
    printf("A(bta)/Z: ");
    radtodeg(aob);
    printf("("); radtodeg(DPI-aob);
    printf(")/"); radtodeg(zob);
    printf("\n");
#endif
    return 0;
}

// azimuth: north=zero, east=90deg

// parallactic angle: iauHd2pa ( ha, dec, phi );

// refraction coefficients: iauRefco

// iauAe2hd ( az, el, phi, &ha, &dec ); A,H -> HA,DEC; phi - site latitude
// iauHd2ae ( ha, dec, phi, &az, &el ); HA,DEC -> A,H

// iauAtoc13 - obs->ICRS(catalog)
// iauAtoi13 - obs->CIRS

// iauAtio13 - CIRS->observed

#if 0
/**
 * convert geocentric coordinates (nowadays, CIRS) to mean (JD2000, ICRS)
 * appRA, appDecl in seconds
 * r, d in seconds
 */
void JnowtoJ2000(double appRA, double appDecl, double *r, double *dc){
    double ra=0., dec=0., utc1, utc2, tai1, tai2, tt1, tt2, fd, eo, ri;
    int y, m, d, H, M;
    DBG("appRa: %g'', appDecl'': %g", appRA, appDecl);
    appRA *= DS2R;
    appDecl *= DAS2R;
#define SOFA(f, ...) do{if(f(__VA_ARGS__)){WARNX("Error in " #f); goto rtn;}}while(0)
    // 1. convert system JDate to UTC
    SOFA(iauJd2cal, JDate, 0., &y, &m, &d, &fd);
    fd *= 24.;
    H = (int)fd;
    fd = (fd - H)*60.;
    M = (int)fd;
    fd = (fd - M)*60.;
    SOFA(iauDtf2d, "UTC", y, m, d, H, M, fd, &utc1, &utc2);
    SOFA(iauUtctai, utc1, utc2, &tai1, &tai2);
    SOFA(iauTaitt, tai1, tai2, &tt1, &tt2);
    iauAtic13(appRA, appDecl, tt1, tt2, &ri, &dec, &eo);
    ra = iauAnp(ri + eo);
    ra *= DR2S;
    dec *= DR2AS;
    DBG("SOFA: r=%g'', d=%g''", ra, dec);
#undef SOFA
rtn:
    if(r) *r = ra;
    if(dc) *dc = dec;
}

/**
 * @brief J2000toJnow - convert ra/dec between epochs
 * @param in  - J2000 (degrees)
 * @param out - Jnow  (degrees)
 * @return
 */
int J2000toJnow(const polar *in, polar *out){
    if(!out) return 1;
    double utc1, utc2;
    time_t tsec;
    struct tm *ts;
    tsec = time(0); // number of seconds since the Epoch, 1970-01-01 00:00:00 +0000 (UTC)
    ts = gmtime(&tsec);
    int result = 0;
    result = iauDtf2d ( "UTC", ts->tm_year+1900, ts->tm_mon+1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, &utc1, &utc2 );
    if (result != 0) {
        fprintf(stderr, "iauDtf2d call failed\n");
        return 1;
    }
    // Make TT julian date for Atci13 call
    double tai1, tai2;
    double tt1, tt2;
    result = iauUtctai(utc1, utc2, &tai1, &tai2);
    if(result){
        fprintf(stderr, "iauUtctai call failed\n");
        return 1;
    }
    result = iauTaitt(tai1, tai2, &tt1, &tt2);
    if(result){
        fprintf(stderr, "iauTaitt call failed\n");
        return 1;
    }
    double pr = 0.0;     // RA proper motion (radians/year; Note 2)
    double pd = 0.0;     // Dec proper motion (radians/year)
    double px = 0.0;     // parallax (arcsec)
    double rv = 0.0;     // radial velocity (km/s, positive if receding)
    double rc = DD2R * in->ra, dc = DD2R * in->dec; // convert into radians
    double ri, di, eo;
    iauAtci13(rc, dc, pr, pd, px, rv, tt1, tt2, &ri, &di, &eo);
    out->ra  = iauAnp(ri - eo) * DR2D;
    out->dec = di * DR2D;
    return 0;
}
#endif
