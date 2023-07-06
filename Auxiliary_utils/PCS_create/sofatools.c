/*
 * This file is part of the PCS_create project.
 * Copyright 2023 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <usefull_macros.h>
#include "sofatools.h"

static placeData *pldata = NULL;
// temporal stubs for weather/place/DUT1 data; return 0 if all OK
placeData *getPlace(){
    if(pldata) return pldata;
    pldata = malloc(sizeof(placeData));
    /* Site longitude, latitude (radians) and height above the geoid (m). */
    pldata->slong = 0.7232763200;
    pldata->slat  = 0.7618977414;
    pldata->salt = 2070.0; // altitude
    return pldata;
}
static placeWeather W = {0};
// set weather parameters: pressure, temperature and humidity
void setWeath(double P, double T, double H){
    W.php = P;
    W.tc = T;
    W.relhum = H;
}
int getWeath(placeWeather *w){
    if(!w) return 0;
    memcpy(w, &W, sizeof(placeWeather));
    return 0;
}
int getDUT(almDut *a){
    if(!a) return 0;
    a->px = a->py = 0;
    a->DUT1 = -0.25080;
    return 0;
}

char *radtodeg(double r){
    static char buf[128];
    int i[4]; char pm;
    r = eraAnpm(r); // normalize angle into range +/- pi
    eraA2af(2, r, &pm, i);
    snprintf(buf, 128, "%c%02d %02d %02d.%02d", pm, i[0],i[1],i[2],i[3]);
    return buf;
}

char *radtohrs(double r){
    static char buf[128];
    int i[4]; char pm;
    r = eraAnp(r); // normalize angle into range 0 to 2pi
    eraA2tf(2, r, &pm, i);
    snprintf(buf, 128, "%02d:%02d:%02d.%02d", i[0],i[1],i[2],i[3]);
    return buf;
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
    if(eraDtf2d("UTC", y, m, d, tms.tm_hour, tms.tm_min, tSeconds, &utc1, &utc2) < 0) return -1;
    if(!MJD) return 0;
    MJD->MJD = utc1 - 2400000.5 + utc2;
    MJD->utc1 = utc1;
    MJD->utc2 = utc2;
    //DBG("UTC(m): %g, %.8f\n", utc1 - 2400000.5, utc2);
    if(eraUtctai(utc1, utc2, &MJD->tai1, &MJD->tai2)) return -1;
    //DBG("TAI");
    if(eraTaitt(MJD->tai1, MJD->tai2, &MJD->tt1, &MJD->tt2)) return -1;
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
    if(eraUtcut1(mjd->utc1, mjd->utc2, dUT1, &ut11, &ut12)) return 1;
    /*double era = iauEra00(ut11, ut12) + slong;
    double eo = iauEe06a(mjd->tt1, mjd->tt2);
    printf("ERA = %s; ", radtohrs(era));
    printf("ERA-eo = %s\n", radtohrs(era-eo));*/
    if(!LST) return 0;
    double ST = eraGst06a(ut11, ut12, mjd->tt1, mjd->tt2);
    ST += slong;
    //if(ST > ERFA_D2PI) ST -= ERFA_D2PI;
    //else if(ST < 0.) ST += ERFA_D2PI;
    *LST = eraAnp(ST); // 0..2pi
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
    eraAe2hd(h->az, ERFA_DPI/2. - h->zd, p->slat, &pc->ha, &pc->dec); // A,H -> HA,DEC; phi - site latitude
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
    eraHd2ae(pc->ha, pc->dec, p->slat, &h->az, &alt);
    h->zd = ERFA_DPI/2. - alt;
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
    eraHd2ae(ha, pc->dec, p->slat, &h->az, &alt);
    h->zd = ERFA_DPI/2. - alt;
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
    placeData *p = getPlace();
    placeWeather w;
    almDut d;
    if(!p) return -1;
    if(getWeath(&w)) return -1;
    if(getDUT(&d)) return -1;
    /* Effective wavelength (microns) */
    double wl = 0.55;
    /* ICRS to observed. */
    double aob, zob, hob, dob, rob, eo;
    if(eraAtco13(p2000->ra, p2000->dec,
                 pr, pd, px, rv,
                 MJD.utc1, MJD.utc2,
                 d.DUT1,
                 p->slong, p->slat, p->salt,
                 d.px, d.py,
                 w.php, w.tc, w.relhum,
                 wl,
                 &aob, &zob,
                 &hob, &dob, &rob, &eo)) return -1;
    DBG("(RA/HA/DEC) J2000: %g/%g/%g; Jnow: %g/%g/%g", p2000->ra, p2000->ha, p2000->dec, rob, hob, dob);
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
    return 0;
}






#if 0
typedef struct{
    double ra;
    double dec;
} polar;


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
