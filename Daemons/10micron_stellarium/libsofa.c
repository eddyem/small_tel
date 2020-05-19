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
#include "usefull_macros.h"

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

// temporal stubs for weather/place/DUT1 data; return 0 if all OK
int getPlace(placeData *p){
    if(!p) return 0;
    /* Site longitude, latitude (radians) and height above the geoid (m). */
    p->slong = 0.7232763200;
    p->slat  = 0.7618977414;
/*
    iauAf2a('+', 41, 26, 26.45, &p->slong); // longitude
    iauAf2a('+', 43, 39, 12.69, &p->slat); // latitude
                        */
    p->salt = 2070.0; // altitude
    //DBG("long: %.10f, lat: %.10f", p->slong, p->slat);
    return 0;
}
int getWeath(placeWeather *w){
    if(!w) return 0;
    w->relhum = 0.7;
    w->tc = 1.;
    w->php = 780.;
    return 0;
}
int getDUT(almDut *a){
    if(!a) return 0;
    a->px = a->py = a->DUT1 = 0.;
    return 0;
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
 * @brief get_ObsPlace - calculate observed place (without PM etc) for given date @550nm
 * @param tval  (i) - time
 * @param p2000 (i) - polar coordinates for J2000 (only ra/dec used), ICRS (catalog)
 * @param pnow  (o) - polar coordinates for given epoch (or NULL)
 * @param hnow  (o) - horizontal coordinates for given epoch (or NULL)
 * @return 0 if all OK
 */
int get_ObsPlace(struct timeval *tval, polarCrds *p2000, polarCrds *pnow, horizCrds *hnow){
    double pr = 0.0;     // RA proper motion (radians/year; Note 2)
    double pd = 0.0;     // Dec proper motion (radians/year)
    double px = 0.0;     // parallax (arcsec)
    double rv = 0.0;     // radial velocity (km/s, positive if receding)
    sMJD MJD;
    if(get_MJDt(tval, &MJD)) return -1;
    if(!p2000) return -1;
    placeData p;
    placeWeather w;
    almDut d;
    if(getPlace(&p)) return -1;
    if(getWeath(&w)) return -1;
    if(getDUT(&d)) return -1;
    /* Effective wavelength (microns) */
    double wl = 0.55;
    /* ICRS to observed. */
    double aob, zob, hob, dob, rob, eo;
/*
    DBG("iauAtco13(%g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g)",
        p2000->ra, p2000->dec, pr, pd, px, rv, MJD.utc1, MJD.utc2, d.DUT1, p.slong, p.slat, p.salt,
                         d.px, d.py, w.php, w.tc, w.relhum, wl);
*/
    if(iauAtco13(p2000->ra, p2000->dec,
                 pr, pd, px, rv,
                 MJD.utc1, MJD.utc2,
                 d.DUT1,
                 p.slong, p.slat, p.salt,
                 d.px, d.py,
                 w.php, w.tc, w.relhum,
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
