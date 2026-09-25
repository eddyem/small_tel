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

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <usefull_macros.h>

#include "fitshdr.h"
#include "mount.h"

static char *hdname = NULL;

#ifndef FLT_EPSILON
#define FLT_EPSILON 1e-6
#endif

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
    time_t curtime = time(NULL);
    bool haveweather = false;
    bool poweredON = (HDR->status == MNT_S_ERROR || HDR->status == MNT_S_OFF) ? false : true;
    // weather block
    if(HDR->weather.last_update < (int)(1 + MOUNT_CHECK_T * 2)){ // weather data is good
        if(time(NULL) - lastcorr > CORRECTIONS_TIMEDIFF){ // make correction once per hour
            if(mount_corrdata(&HDR->weather)) lastcorr = time(NULL);
        }
        haveweather = true;
    }
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
    char val[23];
    val[22] = 0;
#define WRHDR(k, v, c)  do{if(printhdr(fd, k, v, c)){goto returning;}}while(0)
    WRHDR("TIMESYS", "'UTC'", "Time system");
    WRHDR("ORIGIN", "'SAO RAS'", "Organization responsible for the data");
    WRHDR("MOUNTNAM", HDR->mountname, "Mount name");
    if(fabs(HDR->dut.px) > FLT_EPSILON){
        snprintf(val, 22, "%.10f", HDR->dut.px);
        WRHDR("POLARX", val, "IERS pole X coordinate, arcsec");
    }
    if(fabs(HDR->dut.py) > FLT_EPSILON){
        snprintf(val, 22, "%.10f", HDR->dut.py);
        WRHDR("POLARY", val, "IERS pole Y coordinate, arcsec");
    }
    if(fabs(HDR->dut.DUT1) > FLT_EPSILON){
        snprintf(val, 22, "%.10f", HDR->dut.DUT1);
        WRHDR("DUT1", val, "IERS `UT1-UTC`, sec");
    }
    polarCrds_t polar;
    horizCrds_t horiz;
    double pt = mount_getInpCoords(&polar), ht = mount_getInpHor(&horiz);
    if(pt > ht){ // horcrds is older -> show polar
        if(curtime - pt < COORDS_OLD_T){
            snprintf(val, 22, "%.10f", RAD2DEG(polar.ra));
            WRHDR("TAGRA", val, "Target RA (J2000), degrees");
            snprintf(val, 22, "%.10f", RAD2DEG(polar.dec));
            WRHDR("TAGDEC", val, "Target DEC (J2000), degrees");
        }
    }else{ // show horiz
        if(curtime - ht < COORDS_OLD_T){
            snprintf(val, 22, "%.10f", RAD2DEG(horiz.az));
            WRHDR("TAGAZ", val, "Target Azimuth, degrees");
            snprintf(val, 22, "%.10f", RAD2DEG(horiz.zd));
            WRHDR("TAGZD", val, "Target Zenith dist., degrees");
        }
    }
    if(poweredON){
        snprintf(val, 22, "%.10f", RAD2DEG(HDR->polar.ra)); // convert RA to degrees
        WRHDR("RA", val, "Telescope right ascension, current epoch, deg");
        snprintf(val, 22, "%.10f", RAD2DEG(HDR->polar.dec));
        WRHDR("DEC", val, "Telescope declination, current epoch, deg");
        snprintf(val, 22, "%.10f", RAD2DEG(HDR->altaz.az));
        WRHDR("AZ", val, "Telescope azimuth, current epoch, deg");
        snprintf(val, 22, "%.10f", RAD2DEG(HDR->altaz.zd));
        WRHDR("ZD", val, "Telescope zenith distance, current epoch, deg");
    }
    WRHDR("TELSTAT", mount_status_str(HDR->status), "Telescope mount status");
    double mjd;
    mount_getInpMJD(&mjd);
    snprintf(val, 22, "%.10f", 2000.+(mjd - ERFA_DJM00)/365.25); // calculate EPOCH/EQUINOX
    WRHDR("INPEQUIN", val, "Equinox of input celestial coordinate system");
    if(poweredON){
        snprintf(val, 22, "%.10f", 2000.+(HDR->MJD.MJD - ERFA_DJM00)/365.25); // telescope coordinates: JNOW
        WRHDR("EQUINOX", val, "Equinox of telescope celestial coordinate sys.");
        snprintf(val, 22, "%.10f", HDR->MJD.MJD);
        WRHDR("MJD", val, "Modified Julian date of file creation");
        if(*HDR->pierside) WRHDR("PIERSIDE", HDR->pierside, "Pier side of telescope mount");
    }
    snprintf(val, 22, "%.1f", HDR->place.salt);
    WRHDR("ELEVAT", val, "Elevation of site over the sea level");
    snprintf(val, 22, "%.10f", RAD2DEG(HDR->place.slong));
    WRHDR("LONGITUD", val, "Geo longitude of site (east negative)");
    snprintf(val, 22, "%.10f", RAD2DEG(HDR->place.slat));
    WRHDR("LATITUDE", val, "Geo latitude of site (south negative)");
    snprintf(val, 22, "%.4f", RAD2HRS(HDR->sidtime));
    WRHDR("LSTEND", val, "Local sidereal time of file creation");
    if(haveweather){
        snprintf(val, 22, "%.1f", HDR->weather.humidity);
        WRHDR("HUMIDITY", val, "Relative humidity, %%");
        snprintf(val, 22, "%.1f", HDR->weather.pressure);
        WRHDR("PRESSURE", val, "Atmospheric pressure, mmHg");
        snprintf(val, 22, "%.1f", HDR->weather.exttemp);
        WRHDR("EXTTEMP", val, "External temperature, degrC");
        snprintf(val, 22, "%d", HDR->weather.rain);
        WRHDR("RAIN", val, "Rain conditions");
        snprintf(val, 22, "%.1f", HDR->weather.clouds);
        WRHDR("SKYQUAL", val, "Sky quality (0 - wery bad, >2500 - good)");
        snprintf(val, 22, "%.1f", HDR->weather.wind);
        WRHDR("WINDSPD", val, "Wind speed (m/s)");
        snprintf(val, 22, "%.1f", HDR->weather.windmax);
        WRHDR("WINDMAX", val, "Last hour maximal wind speed (m/s)");
        snprintf(val, 22, "%zd", HDR->weather.last_update);
        WRHDR("WEATTIME", val, "Unix time of weather measurements");
    }
    // WRHDR("", , "");
#undef WRHDR
returning:
    close(fd);
    rename(aname, hdname);
}
