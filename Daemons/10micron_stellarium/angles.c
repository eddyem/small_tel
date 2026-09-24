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

#include <erfa.h>
#include <erfam.h>
#include <math.h>
#include <time.h>
#include <usefull_macros.h>

#include "angles.h"

static placeData_t place = {.slong = 0.7232763200, .slat = 0.7618977414, .salt = 2070.};
static almDut_t AlmDut = {0};

// set/get polar/DUT1 data
bool setDUT(almDut_t *D){
    if(!D){
        WARNX("setDUT(): no args");
        return false;
    }
    if(fabs(D->DUT1) > 1.){
        WARNX("setDUT(): DUT1 too large (%g)", D->DUT1);
        return false;
    }
    if(fabs(D->px) > 1000. || fabs(D->py) > 1000.){
        WARNX("setDUT(): bad polar coordinates (%g, %g)", D->px, D->py);
        return false;
    }
    AlmDut = *D;
    return true;
}

void getDUT(almDut_t *D){
    if(!D) return;
    *D = AlmDut;
}


/**
 * @brief setPlaceData - correct place data to given values
 * @param longitude - degrees, (-180, 180], minus to west
 * @param latitude - degrees, [-90, 90], minus to south
 * @param altitude - meters
 * @return false if some of data wrong
 */
bool setPlaceData(double longitude, double latitude, double altitude){
    if(longitude <= -180. || longitude > 180.){
        WARNX("setPlaceData(): bad longitude (%g)", longitude);
        return false;
    }
    if(latitude < -90. || latitude > 90.){
        WARNX("setPlaceData(): bad latitude (%g)", latitude);
        return false;
    }
    if(altitude < -500. || altitude > 9000.){
        WARNX("setPlaceData(): bad altitude (%g)", altitude);
        return false;
    }
    place.salt = altitude;
    place.slat = DEG2RAD(latitude);
    place.slong = DEG2RAD(longitude);
    DBG("Change place data: alt=%gm, lat=%gdeg, long=%gdeg", place.salt, RAD2DEG(place.slat), RAD2DEG(place.slong));
    return true;
}

void getPlaceData(placeData_t *pd){
    if(!pd) return;
    *pd = place;
}

/**
 * @brief ra2str - convert RA to string form "HH:MM:SS.SS"
 * @param ra - RA (hours)
 * @param buf - output buffer
 * @return pointer to buffer
 */
char *ra2str(double ra, char buf[RADEC_STR_MAXLEN]){
    int h = (int)ra;
    ra -= h; ra *= 60.;
    int m = (int)ra;
    ra -= m; ra *= 60.;
    snprintf(buf, RADEC_STR_MAXLEN, "%d:%d:%.2f", h,m,ra);
    buf[RADEC_STR_MAXLEN-1] = 0;
    return buf;
}
/**
 * @brief dec2str - convert Dec to string form "DD:MM:SS.S"
 * @param dec - Dec (degrees)
 * @param buf - output buffer
 * @return pointer to buffer
 */
char *dec2str(double dec, char buf[RADEC_STR_MAXLEN]){
    char sign = '+';
    if(dec < 0){
        sign = '-';
        dec = -dec;
    }
    int d = (int) dec;
    dec -= d; dec *= 60.;
    int dm = (int)dec;
    dec -= dm; dec *= 60.;
    snprintf(buf, RADEC_STR_MAXLEN, "%c%d:%d:%.1f",sign,d,dm,dec);
    buf[RADEC_STR_MAXLEN-1] = 0;
    return buf;
}

// normalize azimuth to [0, 360.) and z.d. to [0, 90]
// if z>90 || z<-90 return false; if z < 0 rotate azimuth to 180
bool normAZ(double *a, double *z){
    if(a){
        norm_angle180(a);
        if(*a < 0.) *a += 360.;
    }
    if(z){
        norm_angle180(z);
        if(*z > 90. || *z < -90.) return false;
        if(*z < 0.){
            *z += 90.;
            *a += 180.;
            if(*a >= 360.) *a -= 360.;
        }
    }
    return true;
}

// normalize RA/DEC to [0..24) for RA and [-90, 90] for DEC
void norm_RA(double *ra){
    if(!ra) return;
    if(*ra >= 0. && *ra < 24.) return;
    double r = fmod(*ra, 24.);
    if(r < 0.) r += 24.;
    *ra = r;
}
void norm_RADEC(double *ra, double *dec){
    if(!ra || !dec) return;
    if(*dec >= -90. && *dec <= 90.){ // need only check RA
        norm_RA(ra);
        return;
    }
    // 1: convert to (-180..+180)
    norm_angle180(dec);
    // 2: fix dec & ra together
    if(*dec > 90.){
        *dec = 180. - *dec;
        *ra += 12.;
        norm_RA(ra);
    }else if(*dec < -90.){
        *dec = -180. - *dec;
        *ra += 12.;
        norm_RA(ra);
    }
}

void norm_RADECr(double *ra, double *dec){
    if(!ra || !dec) return;
    DBG("Ra: %g, Dec: %g", RAD2DEG(*ra), RAD2DEG(*dec));
    if(*dec >= -ERFA_DPI/2. && *dec <= ERFA_DPI/2.){ // need only check RA
        *ra = eraAnp(*ra);
        DBG("Ra become %g", RAD2DEG(*ra));
        return;
    }
    // 1: convert to (-pi..+pi)
    *dec = eraAnpm(*dec);
    // 2: fix dec & ra together
    if(*dec > ERFA_DPI/2.){
        *dec = ERFA_DPI - *dec;
        *ra = eraAnp(*ra + ERFA_DPI);
    }else if(*dec < -ERFA_DPI/2.){
        *dec = -ERFA_DPI - *dec;
        *ra = eraAnp(*ra + ERFA_DPI);
    }
    DBG("Ra become %g, dec become %g", RAD2DEG(*ra), RAD2DEG(*dec));
}

// normalize angle to (-180, 180]
void norm_angle180(double *a){
    if(!a) return;
    double d = fmod(*a + 180.0, 360.0);
    if(d < 0.) d += 360.0;
    else d -= 180;
    *a = d;
}

/**
 * @brief hor2eq  - convert horizontal coordinates to polar
 * @param h (i)   - horizontal coordinates
 * @param pc (o)  - polar coordinates
 * @param sidTime - sidereal time
 */
void hor2eq(horizCrds_t *h, polarCrds_t *pc, double sidTime){
    if(!h || !pc) return;
    DBG("got az=%gdeg, zd=%gdeg; sidtm=%ghrs", RAD2DEG(h->az), RAD2DEG(h->zd), RAD2HRS(sidTime));
    eraAe2hd(h->az, ERFA_DPI/2. - h->zd, place.slat, &pc->ha, &pc->dec); // A,H -> HA,DEC; phi - site latitude
    pc->ha = eraAnp(pc->ha); // normalize to [0,2pi)
    pc->ra = eraAnp(sidTime - pc->ha);
    DBG("dec=%gdeg, ha=%gh, ra=%gh", RAD2DEG(pc->dec), RAD2HRS(pc->ha), RAD2HRS(pc->ra));
    pc->eo = 0.;
}

/**
 * @brief eq2horH - convert polar coordinates to horizontal
 * @param pc (i)  - polar coordinates (only HA used)
 * @param h (o)   - horizontal coordinates
 * @param sidTime - sidereal time
 */
void eq2horH(polarCrds_t *pc, horizCrds_t *h){
    if(!h || !pc) return;
    double alt;
    eraHd2ae(pc->ha, pc->dec, place.slat, &h->az, &alt);
    h->zd = ERFA_DPI/2. - alt;
}

/**
 * @brief eq2hor  - convert polar coordinates to horizontal
 * @param pc (i)  - polar coordinates (only RA used)
 * @param h (o)   - horizontal coordinates
 * @param sidTime - sidereal time
 */
void eq2hor(polarCrds_t *pc, horizCrds_t *h, double sidTime){
    if(!h || !pc) return;
    double ha = sidTime - pc->ra + pc->eo;
    double alt;
    eraHd2ae(ha, pc->dec, place.slat, &h->az, &alt);
    h->zd = ERFA_DPI/2. - alt;
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
    eraA2tf(2, radians, &pm, i);
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
    eraA2af(1, radians, &pm, i);
    snprintf(dms, len, "'%c%02d:%02d:%02d.%d'", pm, i[0],i[1],i[2],i[3]);
}
// the same, but argument in degrees
void d2sDMS(double degrees, char *dms, int len){
    double radians = DEG2RAD(degrees);
    return r2sDMS(radians, dms, len);
}

/**
 * convert str into coordinate (degrees/hours)
 * @param str (i) - string with angle
 * @param val (o) - output angle value
 * @return true if all OK
 */
bool str2coord(const char *str, double *val){
    if(!str || !val) return false;
    int d, m;
    float s;
    int sign = 1;
    if(*str == '+') ++str;
    else if(*str == '-'){
        sign = -1;
        ++str;
    }
    int n = sscanf(str, "%d:%d:%f#", &d, &m, &s);
    if(n != 3){
        DBG("sscanf('%s')=%d", str, 3);
        return false;
    }
    double ang = d + ((double)m)/60. + s/3600.;
    if(sign == -1) *val = -ang;
    else *val = ang;
    return true;
}


/**
 * @brief get_MJDt - calculate MJD of date from argument
 * @param tval (i) - given date (or NULL for current)
 * @param MJD  (o) - time (or NULL just to check)
 * @return true if all OK
 */
bool get_MJDt(struct timeval *tval, sMJD_t *MJD){
    struct tm tms;
    double tSeconds;
    if(!MJD){
        WARNX("get_MJDt(): no output data");
        return false;
    }
    if(!tval){
        struct timespec ts;
        if(clock_gettime(CLOCK_REALTIME, &ts)){
            WARN("clock_gettime()");
            return false;
        }
        gmtime_r(&ts.tv_sec, &tms);
        //localtime_r(&ts.tv_sec, &tms);
        tSeconds = tms.tm_sec + ((double)ts.tv_nsec)/1e9;
    }else{
        gmtime_r(&tval->tv_sec, &tms);
        //localtime_r(&tval->tv_sec, &tms);
        tSeconds = tms.tm_sec + ((double)tval->tv_usec)/1e6;
    }
    int y, m, d;
    y = 1900 + tms.tm_year;
    m = tms.tm_mon + 1;
    d = tms.tm_mday;
    double utc1, utc2;
    /* UTC date. */
    if(eraDtf2d("UTC", y, m, d, tms.tm_hour, tms.tm_min, tSeconds, &utc1, &utc2) < 0){
        WARNX("get_MJDt(): eraDtf2d() error");
        return false;
    }
    //DBG("y=%d, m=%d, d=%d, H=%d, M=%d, seconds=%g, UTC=%g", y, m, d, tms.tm_hour, tms.tm_min, tSeconds, utc1+utc2);
    MJD->MJD = (utc1 - ERFA_DJM0) + utc2;
    MJD->utc1 = utc1;
    MJD->utc2 = utc2;
    //DBG("MJD: %g, %.8f", utc1 - ERFA_DJM0, utc2);
    if(eraUtctai(utc1, utc2, &MJD->tai1, &MJD->tai2)){
        WARNX("get_MJDt(): eraUtctai() error");
        return false;
    }
    //DBG("TAI");
    if(eraTaitt(MJD->tai1, MJD->tai2, &MJD->tt1, &MJD->tt2)){
        WARNX("get_MJDt(): eraTaitt() error");
        return false;
    }
    //DBG("TT");
    return true;
}

/**
 * @brief get_LST - calculate local siderial time
 * @param mjd (i) - date/time for LST (utc1 & tt used)
 * @param dUT1    - (UT1-UTC)
 * @param slong   - site longitude (radians)
 * @param LST (o) - local sidereal time (radians)
 * @return true if all OK
 */
bool get_LST(sMJD_t *mjd, double *LST){
    double ut11, ut12;
    sMJD_t Mjd;
    if(!LST) return false;
    if(!mjd){
        if(!get_MJDt(NULL, &Mjd)) return false;
    }else Mjd = *mjd;
    if(eraUtcut1(Mjd.utc1, Mjd.utc2, AlmDut.DUT1, &ut11, &ut12)) return false;
    double ST = eraGst06a(ut11, ut12, Mjd.tt1, Mjd.tt2);
    //DBG("ST0=%gh; longitude=%gh (%gdeg) STl=%gh", RAD2HRS(ST),
    //    RAD2HRS(place.slong), RAD2DEG(place.slong), RAD2HRS(ST+place.slong));
    ST += place.slong;
    if(ST > ERFA_D2PI) ST -= ERFA_D2PI;
    else if(ST < 0.) ST += ERFA_D2PI;
    *LST = ST;
    return true;
}

/**
 * @brief get_ObsPlace - calculate observed place (without PM etc) for given date @550nm
 * @param tval  (i) - time
 * @param p2000 (i) - polar coordinates for J2000 (only ra/dec used), ICRS (catalog)
 * @param pnow  (o) - polar coordinates for given epoch (or NULL)
 * @param hnow  (o) - horizontal coordinates for given epoch (or NULL)
 * @return true if all OK
 */
bool get_ObsPlace(struct timeval *tval, polarCrds_t *p2000, polarCrds_t *pnow, horizCrds_t *hnow){
    double pr = 0.0;     // RA proper motion (radians/year; Note 2)
    double pd = 0.0;     // Dec proper motion (radians/year)
    double px = 0.0;     // parallax (arcsec)
    double rv = 0.0;     // radial velocity (km/s, positive if receding)
    sMJD_t MJD;
    if(!p2000){
        WARNX("get_ObsPlace(): no input coordinates");
        return false;
    }
    if(!get_MJDt(tval, &MJD)) return false;
    /* Effective wavelength (microns) */
    double wl = 0.55;
    /* ICRS to observed. */
    double aob, zob, hob, dob, rob, eo;
    double p = 1000., t = 0., h = 0.5;
    weather_data_t weath;
    if(get_weather_data(&weath)){
        WARNX("Can't get weather - use default values");
    }else{
        p = 1.3332239 * weath.pressure;
        t = weath.exttemp;
        h = weath.humidity / 100.;
        DBG("pressure=%.1fhPa, temperature=%.1fdegC, humidity=%.1f%%", p, t, h);
    }
    if(eraAtco13(p2000->ra, p2000->dec,
                  pr, pd, px, rv,
                  MJD.utc1, MJD.utc2,
                  AlmDut.DUT1,
                  place.slong, place.slat, place.salt,
                  AlmDut.px, AlmDut.py,
                  p, t, h,
                  wl,
                  &aob, &zob,
                  &hob, &dob, &rob, &eo)) return false;
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
    return true;
}

/**
 * convert geocentric coordinates (nowadays, CIRS) to mean (JD2000, ICRS)
 * in, out - input and output coordinates
 * @return false if failed
 */
bool JnowtoJ2000(const polarCrds_t *in, polarCrds_t *out){
    bool ret = false;
    DBG("appRa: %gdegr, appDecl: %gdegr", RAD2DEG(in->ra), RAD2DEG(in->dec));
#define ERFA(f, ...) do{if(f(__VA_ARGS__)){WARNX("Error in " #f); goto rtn;}}while(0)
    sMJD_t MJD;
    if(!get_MJDt(NULL, &MJD)) return false;
    double ri = 0., di = 0., eo = 0.;
    eraAtic13(in->ra, in->dec, MJD.tt1, MJD.tt2, &ri, &di, &eo);
    if(out){
        out->ha = 0.; out->eo = 0.;
        out->ra = eraAnp(ri + eo);
        out->dec = di;
        DBG("JnowtoJ2000: ra=%gdeg, dec=%gdeg", RAD2DEG(out->ra), RAD2DEG(out->dec));
    }
    ret = true;
#undef ERFA
    return ret;
}

/**
 * @brief J2000toJnow - convert ra/dec between epochs
 * @param in  - J2000 (degrees)
 * @param out - Jnow  (degrees)
 * @return false if failed
 */
bool J2000toJnow(const polarCrds_t *in, polarCrds_t *out){
    DBG("J200RA: %gdegr, J200Dec: %gdegr", RAD2DEG(in->ra), RAD2DEG(in->dec));
    sMJD_t MJD;
    if(!in) return false;
    if(!get_MJDt(NULL, &MJD)) return false;
    // these values could be changed (e.g. by user's input)
    double pr = 0.0;     // RA proper motion (radians/year)
    double pd = 0.0;     // Dec proper motion (radians/year)
    double px = 0.0;     // parallax (arcsec)
    double rv = 0.0;     // radial velocity (km/s, positive if receding)
    double ri, di, eo;
    eraAtci13(in->ra, in->dec, pr, pd, px, rv, MJD.tt1, MJD.tt2, &ri, &di, &eo);
    if(out){
        out->ra  = eraAnp(ri - eo);
        out->dec = di;
    }
    return true;
}



#if 0
typedef enum {
    WEATHER_GOOD = 0,               // may start observations
    WEATHER_BAD = 1,                // cannot start but can continue if want
    WEATHER_TERRIBLE = 2,           // close & park: wind, precipitation, humidity etc.
    WEATHER_PROHIBITED = 3,         // force closing & parking; power off equipment, ready to power off computer
} weather_condition_t;

typedef struct {
    weather_condition_t weather;    // conditions: field "WEATHER"
    float windmax;                  // maximal wind for last hour, m/s: "WINDMAX1"
    float wind;                     // current wind speed, m/s: "WIND"
    float clouds;                   // sky "quality" (>2500 - OK): "CLOUDS"
    float exttemp;                  // external temperature, degC: "EXTTEMP"
    float pressure;                 // atm. pressure, mmHg: "PRESSURE"
    float humidity;                 // humidity, percents: "HUMIDITY"
    int rain;                       // ==1 when rainy: "PRECIP"
    int forceoff;                   // force power off (AC power lost or lightning)
    time_t last_update;             // value of "TMEAS"
} weather_data_t;

int get_weather_data(weather_data_t *data);
#endif
