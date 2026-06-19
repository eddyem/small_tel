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

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <usefull_macros.h>

#include "emulation.h"
#include "mount.h"


#define MNAME_LEN   31

static bool isemulated = false; // ==true for emulation mode

static char mount_name[MNAME_LEN+3] = "'noname'";
static sl_tty_t *mount_dev = NULL;
// device mutex, blocking only in non-local functions
static pthread_mutex_t mntdev_mutex = PTHREAD_MUTEX_INITIALIZER;
// ring buffer for data incoming from serial port
static sl_ringbuffer_t *RBin = NULL;
// status
static atomic_int mountstatus = MNT_S_ERROR;

// input and current target coordinates
static polarCrds_t InpCoords = {0}, // input as user give (for epoch InpMJD)
    TagCoords = {0}; // target for Jnow after command "point to input"
static horizCrds_t InpHoriz = {0};
// input MJD (Modified Julian Date: started from ERFA_DJM0==2400000.5
static double InpMJD = ERFA_DJM00; // J2000
// times of coords/mjd change
static double InpCTime = 0., TagTime = 0., InpHTime = 0., InpMTime = 0.;

// return time of modification
double mount_getInpCoords(polarCrds_t *c){
    if(c) *c = InpCoords;
    return InpCTime;
}
double mount_getTagCoords(polarCrds_t *c){
    if(c) *c = TagCoords;
    return TagTime;
}
double mount_getInpMJD(double *MJD){
    if(MJD) *MJD = InpMJD;
    return InpMTime;
}
double mount_getInpHor(horizCrds_t *c){
    if(c) *c = InpHoriz;
    return InpHTime;
}

// change input coordinates
/**
 * @brief mount_setInpHA - set hour angle
 * @param ha (HOURS!!)
 * @return false if `ha` isn't in [0,24)
 */
bool mount_setInpHA(double ha){
    if(ha < 0. || ha >= 24.) return false;
    InpCoords.ha = HRS2RAD(ha);
    InpCTime = sl_dtime();
    return true;
}
/**
 * @brief mount_setInpRA - set right ascension
 * @param ra (DEGREES!)
 * @return fale if `ra` isn't in [0, 360)
 */
bool mount_setInpRA(double ra){
    if(ra < 0. || ra >= 360.) return false;
    InpCoords.ra = DEG2RAD(ra);
    InpCTime = sl_dtime();
    return true;
}
/**
 * @brief mount_setInpDec - set declination
 * @param dec (DEGREES!)
 * @return false if `dec` isn't in [-90, 90]
 */
bool mount_setInpDec(double dec){
    if(dec < -90. || dec > 90.) return false;
    InpCoords.dec = DEG2RAD(dec);
    InpCTime = sl_dtime();
    return true;
}
/**
 * @brief mount_setInpA - set azimuth
 * @param A (DEGREES)
 * @return false if A isn't in [0, 360)
 */
bool mount_setInpA(double A){
    if(A < 0. || A >= 360.) return false;
    InpHoriz.az = DEG2RAD(A);
    InpHTime = sl_dtime();
    return true;
}
/**
 * @brief mount_setInpZ - set zenith distance
 * @param Z (DEGREES)
 * @return false if Z isn't in [0, 90]
 */
bool mount_setInpZ(double Z){
    if(Z < 0 || Z > 90) return false;
    InpHoriz.zd = DEG2RAD(Z);
    InpHTime = sl_dtime();
    return true;
}
// without checking
bool mount_setInpMJD(double m){
    InpMJD = m;
    InpMTime = sl_dtime();
    return true;
}

/**
 * @brief mount_set_name - set mount name for FITS header
 * @param name - new string with name
 * @return false if failed
 */
bool mount_set_name(const char *name){
    if(!name || !*name) return false;
    int l = strlen(name);
    if(l > MNAME_LEN) return false;
    sprintf(mount_name, "'%s'", name);
    return true;
}

/**
 * @brief mount_set_dev - open mount device without checking that mount is alive
 * @param dev - path to device
 * @param speed - baudrate
 * @return false if failed to open device `dev`
 */
bool mount_set_dev(char *dev, int speed, int timeout){
    pthread_mutex_lock(&mntdev_mutex);
    if(mount_dev) sl_tty_close(&mount_dev);
    mount_dev = sl_tty_new(dev, speed, 4096);
    if(mount_dev) mount_dev = sl_tty_open(mount_dev, 1);
    if(!mount_dev){
        pthread_mutex_unlock(&mntdev_mutex);
        return false;
    }
    sl_tty_tmout(timeout);
    if(!RBin) RBin = sl_RB_new(BUFSIZ);
    else sl_RB_clearbuf(RBin);
    pthread_mutex_unlock(&mntdev_mutex);
    return true;
}

static const char *statuses[MNT_S_STATAMOUNT] = {
    [MNT_S_TRACKING] = "'Tracking'",
    [MNT_S_STOPHOM] = "'Stopped or homing'",
    [MNT_S_PARKING] = "'Slewing to park'",
    [MNT_S_UNPARKING] = "'Unparking'",
    [MNT_S_HOMING] = "'Slewing to home'",
    [MNT_S_PARKED] = "'Parked'",
    [MNT_S_SLEWING] = "'Slewing or going to stop'",
    [MNT_S_STOPPED] = "'Stopped'",
    [MNT_S_INHIBITED] = "'Motors inhibited, T too low'",
    [MNT_S_OUTLIMIT] = "'Outside tracking limit'",
    [MNT_S_FOLSAT]= "'Following satellite'",
    [MNT_S_DATINCOSIST]= "'Data inconsistency'",
    [MNT_S_ERROR] = "'Error (disconnected?)'"
};

/**
 * @brief strstatus - return string explanation of mount status
 * @param status - integer status code
 * @return statically allocated string with explanation
 */
const char* mount_status_str(){
    int curst = atomic_load(&mountstatus);
    if(curst > -1 && curst < MNT_S_STATAMOUNT) return statuses[curst];
    return "'Unknown status'";
}
// return current mount status
mount_status_t mount_status(){
    return (mount_status_t)atomic_load(&mountstatus);
}

/**
 * @brief write_cmd - try to write command to mount
 * @param cmd - string with command or NULL just to clear all incoming data
 * @return false on write/read error; all read information is in RBin (even if `false` returned, you SHOULD read all available strings from it)
 */
static bool write_cmd(const char *cmd){
    bool ret = true;
    if(cmd){
        size_t l = strlen(cmd);
        if(sl_tty_write(mount_dev->comfd, cmd, l)) ret = false;
    }
    int got = 0;
    while((got = sl_tty_read(mount_dev)) > 0){
        if(sl_RB_write(RBin, (uint8_t*) mount_dev->buf, got)){
            got = -1;
            break;
        }
    }
    if(got < 0) return false;
    return ret;
}

// check if mount connected
static bool chkconn(){
    int r = 0;
    do{ // clear incoming buffer @ start
        r = sl_tty_read(mount_dev);
    }while(r > 0);
    if(r < 0) return false;
    write_cmd("#"); // clear cmd buffer
    sl_RB_clearbuf(RBin);
    bool w = write_cmd(":SB0#");
    sl_RB_clearbuf(RBin);
    return w;
}


// try to guess serial speed & set 115200
static bool guess_speed(){
    if(!mount_dev) return false;
    close(mount_dev->comfd);
#define SPDBUFSZ    7
    const int speeds[SPDBUFSZ] = {57600, 38400, 19200, 9600, 4800, 2400, 1200};
    int idx = 0;
    for(; idx < SPDBUFSZ; ++idx){
        mount_dev->speed = speeds[idx];
        sl_tty_t *trydev = sl_tty_open(mount_dev, 1);
        if(!trydev) continue;
        if(chkconn()) break;
        close(mount_dev->comfd);
    }
    if(idx == SPDBUFSZ) return false; // device not responding
    close(mount_dev->comfd);
    mount_dev->speed = 115200;
    if(!sl_tty_open(mount_dev, 1)) return false;
#undef SPDBUFSZ
    return true;
}

// connect to mount
bool mount_connect(){
    if(isemulated){
        atomic_store(&mountstatus, MNT_S_STOPPED);
        return true;
    }
    if(!mount_dev) return false;
    pthread_mutex_lock(&mntdev_mutex);
    if(!chkconn() && !guess_speed()) return false;
    bool ret = true;
    if(!write_cmd(":STOP#")) ret = false; // stop tracking after poweron
    sl_RB_clearbuf(RBin);
    if(!write_cmd(":U2#")) ret = false; // set high precision
    sl_RB_clearbuf(RBin);
    if(!write_cmd(":So10#")) ret = false; // set minimum altitude to 10 degrees
    sl_RB_clearbuf(RBin);
    pthread_mutex_unlock(&mntdev_mutex);
    if(ret) LOGMSG("Connected to %s@115200", mount_dev->portname);
    else LOGERR("Can't write commands to mount");
    return ret;
}

void mount_disconnect(){
    if(isemulated) return;
    pthread_mutex_trylock(&mntdev_mutex); // at least, try
    if(mount_dev) close(mount_dev->comfd);
    pthread_mutex_unlock(&mntdev_mutex);
}

// point to ra/dec over serial
static bool mount_pointto(double ra, double dec){
    (void) ra; (void) dec;
    ;
    return true;
}

/**
 * send input RA/Decl (j2000!) coordinates to tel
 * ra in hours (0..24), decl in degrees (-90..90)
 * @return true if all OK
 */
bool mount_point(double ra, double dec){
    char buf[RADEC_STR_MAXLEN];
    radec2str(ra, dec, buf);
    DBG("Set RA/Decl to %s", buf);
    LOGMSG("Try to set RA/Decl to %s", buf);
    norm_RADEC(&ra, &dec);
    bool (*pointfunction)(double, double) = mount_pointto;
    if(isemulated) pointfunction = point_emulation;
    return pointfunction(ra, dec);
}

void set_emulation_mode(){
    isemulated = true;
}

mount_status_t mount_getcoords(double *ra, double *dec){
    if(!ra || !dec) return MNT_S_ERROR;
    if(isemulated){
        get_emul_coords(ra, dec);
        DBG("Emulated coordinates: %g, %g", *ra, *dec);
    }else{
        ; // get real coordinates
    }
    return mount_status();
}
