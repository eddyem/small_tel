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
int getWeath(placeWeather *w){
    if(!w) return 0;
    w->relhum = 0.7;
    w->tc = 1.;
    w->php = 780.;
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
    r = iauAnpm(r);
    iauA2af (2, r, &pm, i);
    snprintf(buf, 128, "%c%02d %02d %02d.%02d", pm, i[0],i[1],i[2],i[3]);
    return buf;
}

char *radtohrs(double r){
    static char buf[128];
    int i[4]; char pm;
    r = iauAnp(r);
    iauA2tf(2, r, &pm, i);
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
double get_LST(sMJD *mjd, double dUT1, double slong, double *LST){
    double ut11, ut12;
    if(iauUtcut1(mjd->utc1, mjd->utc2, dUT1, &ut11, &ut12)) return 1;
    double ST = iauGst06a(ut11, ut12, mjd->tt1, mjd->tt2);
    ST += slong;
    if(ST > D2PI) ST -= D2PI;
    if(LST) *LST = ST;
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
    if(iauAtco13(p2000->ra, p2000->dec,
                 pr, pd, px, rv,
                 MJD.utc1, MJD.utc2,
                 d.DUT1,
                 p->slong, p->slat, p->salt,
                 d.px, d.py,
                 w.php, w.tc, w.relhum,
                 wl,
                 &aob, &zob,
                 &hob, &dob, &rob, &eo)) return -1;
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

// Y     M D   MJD      x(arcsec)   y(arcsec)   UT1-UTC(sec)
// 2020  5 15  58984       0.0986      0.4466     -0.25080

int main(){
    sMJD mjd;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if(get_MJDt(&tv, &mjd)) return 1;
    double ST;
    almDut adut;
    if(getDUT(&adut)) return 1;
    placeData *place = getPlace();
    if(!place) return 1;
    if(get_LST(&mjd, adut.DUT1, place->slong, &ST)) return 1;
    printf("ST = %s\n", radtohrs(ST));
    horizCrds htest = {.az = DD2R*91., .zd = DPI/4.}; // Z=45degr, A=1degr from south to west
    printf("hzd=%g\n", htest.zd);
    polarCrds ptest;
    hor2eq(&htest, &ptest, ST);
    printf("A=%s, ", radtodeg(htest.az));
    printf("Z=%s; ", radtodeg(htest.zd));
    printf("HOR->EQ: HA=%s, ", radtohrs(ptest.ha));
    printf("RA=%s, ", radtohrs(ptest.ra));
    printf("DEC=%s\n", radtodeg(ptest.dec));
    horizCrds h2;
    eq2hor(&ptest, &h2, ST);
    printf("Back conversion EQ->HOR: A=%s, ", radtodeg(h2.az));
    printf("Z=%s\n", radtodeg(h2.zd));
    polarCrds pnow;
    if(!get_ObsPlace(&tv, &ptest, &pnow, &h2)){
        printf("\nApparent place, RA=%s, ", radtohrs(pnow.ra-pnow.eo));
        printf("HA=%s, ", radtohrs(pnow.ha));
        printf("ST-RA=%s, ", radtohrs(ST-pnow.ra+pnow.eo));
        printf("DEC=%s; ", radtodeg(pnow.dec));
        printf("A=%s, ", radtodeg(h2.az));
        printf("Z=%s\n", radtodeg(h2.zd));
        polarCrds h2p;
        hor2eq(&h2, &h2p, ST);
        printf("\tHOR->EQ: RA=%s, ", radtohrs(h2p.ra-h2p.eo));
        printf("HA=%s, ", radtohrs(h2p.ha));
        printf("ST-RA=%s, ", radtohrs(ST-h2p.ra+h2p.eo));
        printf("DEC=%s\n", radtodeg(h2p.dec));
        eq2hor(&pnow, &h2, ST);
        //eq2horH(&pnow, &h2);
        printf("\tEQ->HOR: A=%s, ", radtodeg(h2.az));
        printf("Z=%s\n", radtodeg(h2.zd));
    }
    return 0;
}

