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
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <usefull_macros.h>

#include "fitshdr.h"
#include "mount.h"

static char *hdname = NULL;

/**
 * @brief set_header_name - check ability of witing into file and set given name
 * @param name - header filename
 * @return false if can't write into `name`
 */
bool set_header_name(const char *name){
    if(!name) return false;
    int fd;
    if((fd = open(name, O_WRONLY | O_TRUNC | O_CREAT, 0644)) < 0){ // test FITS-header file for writing
        WARN("Can't open %s for writing", name);
        return false;
    }
    close(fd);
    FREE(hdname);
    hdname = strdup(name);
    return true;
}

/**
 * @brief printhdr - write FITS record into output file
 * @param fd   - fd to write
 * @param key  - key
 * @param val  - value
 * @param cmnt - comment
 * @return 0 if all OK
 */
static int printhdr(int fd, const char *key, const char *val, const char *cmnt){
    char tmp[81];
    char tk[9];
    if(strlen(key) > 8){
        snprintf(tk, 8, "%s", key);
        tk[8] = 0;
        key = tk;
    }
    size_t L = 0;
    if(cmnt){
        L = snprintf(tmp, 81, "%-8s= %-21s / %s", key,  val, cmnt);
    }else{
        L = snprintf(tmp, 81, "%-8s= %s", key, val);
    }
    if(L > 80){
        tmp[80] = 0;
        L = 80;
    }
    tmp[L++] = '\n';
    if(write(fd, tmp, L) != (ssize_t)L){
        WARN("write()");
        return 1;
    }
    return 0;
}

/**
 * @brief wrhdr - try to write into header file
 */
void wrhdr(fitsheader_t *HDR){
    if(!HDR) return;
    static time_t lastcorr = 0; // last time of time/weather corrections sent to mount
    double curtime = sl_dtime();
    //bool haveweather = false;
    bool poweredON = (HDR->status == MNT_S_ERROR || HDR->status == MNT_S_OFF) ? false : true;
    // weather block
    if(time(NULL) - HDR->weather.last_update < (int)(1. + MOUNT_CHECK_T * 10.)){ // weather data is good
        if(poweredON && time(NULL) - lastcorr > CORRECTIONS_TIMEDIFF){ // make correction once per hour
            if(mount_corrdata(&HDR->weather)) lastcorr = time(NULL);
        }
        //haveweather = true;
    }else WARNX("bad weather; curtime - HDR->weather.last_update = %g", curtime - HDR->weather.last_update);
    if(!hdname){
        DBG("hdname not given!");
        return;
    }
    char aname[PATH_MAX];
    size_t L = snprintf(aname, PATH_MAX, "%sXXXXXX", hdname);
    if(L == PATH_MAX) aname[PATH_MAX-1] = 0;
    int fd = mkstemp(aname);
    if(fd < 0){
        WARN("Can't write header file: mkstemp()");
        return;
    }
    fchmod(fd, 0644);
#define VALLEN 22
#define CMNTLEN 51
    char val[VALLEN+1];
    char ang[RADEC_STR_MAXLEN], cmnt[CMNTLEN+1];
    val[VALLEN] = 0, cmnt[CMNTLEN] = 0;
#define WRHDR(k, v, c)  do{if(printhdr(fd, k, v, c)){goto returning;}}while(0)
    WRHDR("TIMESYS", "'UTC'", "Time system");
    WRHDR("ORIGIN", "'SAO RAS'", "Organization responsible for the data");
    WRHDR("MOUNTNAM", HDR->mountname, "Mount name");
    if(fabs(HDR->dut.px) > FLT_EPSILON){
        snprintf(val, VALLEN, "%.10f", HDR->dut.px);
        WRHDR("POLARX", val, "IERS pole X coordinate, arcsec");
    }
    if(fabs(HDR->dut.py) > FLT_EPSILON){
        snprintf(val, VALLEN, "%.10f", HDR->dut.py);
        WRHDR("POLARY", val, "IERS pole Y coordinate, arcsec");
    }
    if(fabs(HDR->dut.DUT1) > FLT_EPSILON){
        snprintf(val, VALLEN, "%.10f", HDR->dut.DUT1);
        WRHDR("DUT1", val, "IERS `UT1-UTC`, sec");
    }
    polarCrds_t polar;
    horizCrds_t horiz;
    double pt = mount_getInpCoords(&polar), ht = mount_getInpHor(&horiz);
    if(pt > ht){ // horcrds is older -> show polar
        if(curtime - pt < COORDS_OLD_T){
            snprintf(val, VALLEN, "%.10f", RAD2DEG(polar.ra));
            r2sHMS(polar.ra, ang);
            snprintf(cmnt, CMNTLEN, "Input RA, deg. (%s)", ang);
            WRHDR("INPRA", val, cmnt);
            snprintf(val, VALLEN, "%.10f", RAD2DEG(polar.dec));
            r2sDMS(polar.dec, ang);
            snprintf(cmnt, CMNTLEN, "Input DEC, deg. (%s)", ang);
            WRHDR("INPDEC", val, cmnt);
        }
    }else{ // show horiz
        if(curtime - ht < COORDS_OLD_T){
            snprintf(val, VALLEN, "%.10f", RAD2DEG(horiz.az));
            r2sDMS(horiz.az, ang);
            snprintf(cmnt, CMNTLEN, "Input Azimuth, deg. (%s)", ang);
            WRHDR("INPAZ", val, cmnt);
            snprintf(val, VALLEN, "%.10f", RAD2DEG(horiz.zd));
            r2sDMS(horiz.zd, ang);
            snprintf(cmnt, CMNTLEN, "Input Zenith dist., deg. (%s)", ang);
            WRHDR("INPZD", val, cmnt);
        }
    }
    if(curtime - mount_getTagCoords(&polar) < COORDS_OLD_T){
        snprintf(val, VALLEN, "%.10f", RAD2DEG(polar.ra));
        r2sHMS(polar.ra, ang);
        snprintf(cmnt, CMNTLEN, "Target RA, deg. (%s)", ang);
        WRHDR("TAGRA", val, cmnt);
        snprintf(val, VALLEN, "%.10f", RAD2DEG(polar.dec));
        r2sDMS(polar.dec, ang);
        snprintf(cmnt, CMNTLEN, "Target DEC, deg. (%s)", ang);
        WRHDR("TAGDEC", val, cmnt);
    }
    if(poweredON){
        snprintf(val, VALLEN, "%.10f", RAD2HRS(HDR->polar.ra)); // convert RA to degrees
        r2sHMS(HDR->polar.ra, ang);
        snprintf(cmnt, CMNTLEN, "Telescope Right Ascension, deg. (%s)", ang);
        WRHDR("RA", val, cmnt);
        double ha = eraAnp(HDR->sidtime - HDR->polar.ra);
        snprintf(val, VALLEN, "%.10f", RAD2HRS(ha)); // hour angle
        r2sHMS(ha, ang);
        snprintf(cmnt, CMNTLEN, "Telescope Hour Angle, deg. (%s)", ang);
        WRHDR("HA", val, cmnt);
        snprintf(val, VALLEN, "%.10f", RAD2DEG(HDR->polar.dec));
        r2sDMS(HDR->polar.dec, ang);
        snprintf(cmnt, CMNTLEN, "Telescope Declination, deg. (%s)", ang);
        WRHDR("DEC", val, cmnt);
        snprintf(val, VALLEN, "%.10f", RAD2DEG(HDR->altaz.az));
        r2sDMS(HDR->altaz.az, ang);
        snprintf(cmnt, CMNTLEN, "Telescope Azimuth, deg. (%s)", ang);
        WRHDR("AZ", val, cmnt);
        snprintf(val, VALLEN, "%.10f", RAD2DEG(HDR->altaz.zd));
        r2sDMS(HDR->altaz.zd, ang);
        snprintf(cmnt, CMNTLEN, "Telescope Zenith Distance, deg. (%s)", ang);
        WRHDR("ZD", val, cmnt);
    }
    WRHDR("TELSTAT", mount_status_str(HDR->status), "Telescope mount status");
    double epoch;
    mount_getInpEpoch(&epoch);
    if(epoch > 0.) snprintf(val, VALLEN, "%.8f", epoch); // calculate EPOCH/EQUINOX
    else snprintf(val, VALLEN, "%.10f", 2000.+(HDR->MJD.MJD - ERFA_DJM00)/365.25);
    WRHDR("INPEQUIN", val, "Equinox of input celestial coordinate system");
    if(poweredON){
        snprintf(val, VALLEN, "%.10f", 2000.+(HDR->MJD.MJD - ERFA_DJM00)/365.25); // telescope coordinates: JNOW
        WRHDR("EQUINOX", val, "Equinox of telescope celestial coordinate sys.");
        snprintf(val, VALLEN, "%.10f", HDR->MJD.MJD);
        WRHDR("MJD", val, "Modified Julian date of file creation");
        if(*HDR->pierside) WRHDR("PIERSIDE", HDR->pierside, "Pier side of telescope mount");
    }
    snprintf(val, VALLEN, "%.1f", HDR->place.salt);
    WRHDR("ELEVAT", val, "Elevation of site over the sea level");
    snprintf(val, VALLEN, "%.10f", RAD2DEG(HDR->place.slong));
    WRHDR("LONGITUD", val, "Geo longitude of site (east negative)");
    snprintf(val, VALLEN, "%.10f", RAD2DEG(HDR->place.slat));
    WRHDR("LATITUDE", val, "Geo latitude of site (south negative)");
    snprintf(val, VALLEN, "%.6f", RAD2HRS(HDR->sidtime));
    WRHDR("LSTEND", val, "Local sidereal time of file creation");
    /* Weather data is written into separate header-file by weather daemon
    if(haveweather){
        snprintf(val, VALLEN, "%.1f", HDR->weather.humidity);
        WRHDR("HUMIDITY", val, "Relative humidity, %%");
        snprintf(val, VALLEN, "%.1f", HDR->weather.pressure);
        WRHDR("PRESSURE", val, "Atmospheric pressure, mmHg");
        snprintf(val, VALLEN, "%.1f", HDR->weather.exttemp);
        WRHDR("EXTTEMP", val, "External temperature, degrC");
        snprintf(val, VALLEN, "%d", HDR->weather.rain);
        WRHDR("RAIN", val, "Rain conditions");
        snprintf(val, VALLEN, "%.1f", HDR->weather.clouds);
        WRHDR("SKYQUAL", val, "Sky quality (0 - wery bad, >2500 - good)");
        snprintf(val, VALLEN, "%.1f", HDR->weather.wind);
        WRHDR("WINDSPD", val, "Wind speed (m/s)");
        snprintf(val, VALLEN, "%.1f", HDR->weather.windmax);
        WRHDR("WINDMAX", val, "Last hour maximal wind speed (m/s)");
        snprintf(val, VALLEN, "%lld", (long long)HDR->weather.last_update);
        WRHDR("WEATTIME", val, "Unix time of weather measurements");
    }*/
    // WRHDR("", , "");
#undef WRHDR
returning:
    close(fd);
    rename(aname, hdname);
}
