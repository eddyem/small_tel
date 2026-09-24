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

#include "10micron_commands.h"
#include "emulation.h"
#include "mount.h"


#define MNAME_LEN   31

static bool isemulated = false; // ==true for emulation mode

// default serial timeout, seconds
static double sertmout = 1.;

static char mount_name[MNAME_LEN+3] = "'noname'";
static sl_tty_t *mount_dev = NULL;
// device mutex, blocking only in non-local functions
static pthread_mutex_t mntdev_mutex = PTHREAD_MUTEX_INITIALIZER;
// status
static atomic_int mountstatus = MNT_S_ERROR;

// parking coordinates
static horizCrds_t ParkCoords = {.az = 0., .zd = DEG2RAD(80.)};
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
 * @param ra (HOURS!!)
 * @return fale if `ra` isn't in [0, 24)
 */
bool mount_setInpRA(double ra){
    if(ra < 0. || ra >= 24.) return false;
    InpCoords.ra = HRS2RAD(ra);
    InpCTime = sl_dtime();
    return true;
}
/**
 * @brief mount_setInpDec - set declination
 * @param dec (DEGREES!!)
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
 * @param timeout - timeout (seconds) for answer's waiting
 * @return false if failed to open device `dev`
 */
bool mount_set_dev(char *dev, int speed, double timeout){
    pthread_mutex_lock(&mntdev_mutex);
    if(mount_dev) sl_tty_close(&mount_dev);
    mount_dev = sl_tty_new(dev, speed, 4096);
    if(mount_dev) mount_dev = sl_tty_open(mount_dev, 1);
    if(!mount_dev){
        pthread_mutex_unlock(&mntdev_mutex);
        DBG("Can't init serial device");
        return false;
    }
    DBG("Mount device inited and opened");
    // set terminal timeout to `timeout / 10` or 100ms
    int usecs = (timeout < 1.) ? (int)(timeout * 1e5) : 100000;
    sl_tty_tmout(usecs);
    sertmout = timeout;
    DBG("set timeout of answer waiting to %gs", sertmout);
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
    [MNT_S_ERROR] = "'Error'",
    [MNT_S_OFF] = "'Off'"
};

/**
 * @brief strstatus - return string explanation of mount status
 * @param status - integer status code
 * @return statically allocated string with explanation
 */
const char* mount_status_str(mount_status_t st){
    if(st < MNT_S_STATAMOUNT) return statuses[st];
    return "'Unknown status'";
}

/**
 * @brief send_cmd_resp - send command and wait for '#'-terminated response
 * @param cmd     - command (e.g. ":Sr12.345#")
 * @param resp    - buffer for response (may be NULL)
 * @param resplen - its size
 * @return true if '#'-terminated response received before timeout
 */
static bool send_cmd_resp(const char *cmd, char *resp, size_t resplen){
    if(!mount_dev) return false;
    while(sl_tty_read(mount_dev) > 0); // clear tty buffer
    if(!cmd) return false; // just clear buffer when cmd==NULL
    DBG("Command '%s', fd: %d", cmd, mount_dev->comfd);
    if(sl_tty_write(mount_dev->comfd, cmd, strlen(cmd))){
        WARN("sl_tty_write()");
        return false;
    }
    if(resp && resplen) resp[0] = 0;
    size_t pos = 0;
    double t0 = sl_dtime();
    bool gotEOM = false;
    while(sl_dtime() - t0 < sertmout){
        int got = sl_tty_read(mount_dev);
        if(got < 0){
            WARN("sl_tty_read()");
            if(resp && resplen) resp[pos < resplen ? pos : resplen-1] = 0;
            DBG("pos: %zd, resp: '%s'", pos, resp);
            return false;
        } else if(pos && got == 0) break; // break if we already receive the message
        for(int i = 0; i < got; ++i){
            char c = mount_dev->buf[i];
            if(c == '\r' || c == '\n') continue; // WTF?
            if(c == '#'){ // end of message
                gotEOM = true;
                //if(resp && resplen) resp[pos < resplen ? pos : resplen-1] = 0;
                //DBG("Got end of message: '%s' (waited for %gs)", resp, );
                //return true;
                break;
            }
            if(resp && pos + 1 < resplen) resp[pos++] = c;
        }
    }
    DBG("Waited for %gs", sl_dtime() - t0);
    if(resp && resplen){
        resp[pos < resplen ? pos : resplen-1] = 0;
        DBG("pos: %zd, resp: '%s'", pos, resp);
        // 10Micron/LX200-like: '0' or 'E'/'e' -> error; '1' - OK
        if(gotEOM || (pos == 1 && resp[0] == '1')) return true;
    }
    return false;
}

/**
 * @brief write_cmd - try to write command to mount and check answer for not false/error
 * @param cmd - string with command or NULL just to clear all incoming data
 * @param haveanswer - true if mount have to answer to `cmd`
 * @return false on write/read error or error in answer
 */
static bool write_cmd(const char *cmd, bool haveanswer){
    char buf[128];
    if(!haveanswer){
        DBG("Command '%s'", cmd);
        if(!mount_dev || !cmd){
            DBG("no mount device?");
            return false;
        }
        if(sl_tty_write(mount_dev->comfd, cmd, strlen(cmd))){
            DBG("Error writing request '%s'", cmd);
            return false;
        }
        while(sl_tty_read(mount_dev) > 0);
    }else if(!send_cmd_resp(cmd, buf, 128)){
        DBG("Error writing command '%s'", cmd);
        return false;
    }
    return true;
}

// return current mount status
mount_status_t mount_status(){
    mount_status_t curst = (mount_status_t)atomic_load(&mountstatus);
    if(curst == MNT_S_OFF) return curst; // do nothing when mount if off
    if(!mount_dev){
        atomic_store(&mountstatus, MNT_S_OFF);
        return MNT_S_OFF;
    }
    char resp[64];
    pthread_mutex_lock(&mntdev_mutex);
    int ntry = 0;
    for(; ntry < 3; ++ntry)
        if(send_cmd_resp(CMD_GETSTAT, resp, 64)) break;
    if(ntry == 3){ // mount not responded -> set status OFF
        curst = MNT_S_OFF;
    }else{ // parsing of status
        char *nxt = NULL;
        long statN = strtol(resp, &nxt, 10);
        //DBG("resp=%s, nxt=%s, stat=%ld", resp, nxt, statN);
        if(!nxt || *nxt || statN >= MNT_S_ERROR || statN < 0) curst = MNT_S_ERROR; // wrong answer
        else curst = (mount_status_t) statN;
        DBG("STATUS: %s (%d)", statuses[curst], curst);
    }
    pthread_mutex_unlock(&mntdev_mutex);
    atomic_store(&mountstatus, curst);
    return curst;
}

// check if mount connected
static bool chkconn(){
    FNAME();
    int r = 0;
    do{ // clear incoming buffer @ start
        r = sl_tty_read(mount_dev);
    }while(r > 0);
    if(r < 0){
        WARN("Error reading");
        DBG("error reading: got %d", r);
        return false;
    }
    write_cmd("#", false); // clear cmd buffer
    bool ret = false;
    for(int i = 0; i < 5; ++i){
        DBG("Try %d", i+1);
        ret = write_cmd(CMD_BAUDRATE, true);
        if(ret) break;
    }
    return ret;
}

// try to guess serial speed & set 115200
static bool guess_speed(){
    if(!mount_dev) return false;
    close(mount_dev->comfd);
#define SPDBUFSZ    7
    const int speeds[SPDBUFSZ] = {57600, 38400, 19200, 9600, 4800, 2400, 1200};
    int idx = 0;
    for(; idx < SPDBUFSZ; ++idx){
        DBG("try speed %d", speeds[idx]);
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
    DBG("OK, opened @ 115200");
#undef SPDBUFSZ
    return true;
}

// connect to mount
bool mount_connect(){
    if(isemulated) return true;
    if(!mount_dev) return false;
    pthread_mutex_lock(&mntdev_mutex);
    if(!chkconn() && !guess_speed()){
        pthread_mutex_unlock(&mntdev_mutex);
        return false;
    }
    bool ret = true;
    if(!write_cmd(CMD_STOP, false)) ret = false; // stop tracking after poweron
    if(!write_cmd(CMD_HIGHPREC, false)) ret = false; // set high precision
    char buf[64];
    snprintf(buf, 63, CMD_SETMINALT, 10);
    if(!write_cmd(buf, true)) ret = false; // set minimum altitude to 10 degrees
    pthread_mutex_unlock(&mntdev_mutex);
    if(ret) LOGMSG("Connected to %s@115200", mount_dev->portname);
    else LOGERR("Can't write commands to mount");
    return ret;
}

void mount_disconnect(){
    if(isemulated){
        emulation_stop();
        return;
    }
    pthread_mutex_trylock(&mntdev_mutex); // at least, try
    if(mount_dev) close(mount_dev->comfd);
    pthread_mutex_unlock(&mntdev_mutex);
}

/**
 * TODO: change angles to RAD!
 * send input RA/Decl (j2000!) coordinates to tel
 * ra in hours (0..24), decl in degrees (-90..90)
 * @return true if all OK
 */
bool mount_point(double ra, double dec){
    char rastr[RADEC_STR_MAXLEN], decstr[RADEC_STR_MAXLEN];
    norm_RADEC(&ra, &dec);
    ra2str(ra, rastr);
    dec2str(dec, decstr);
    DBG("Set RA/Decl to %s/%s", rastr, decstr);
    LOGMSG("Try to set RA/Decl to %s/%s", rastr, decstr);
    if(isemulated) return point_emulation(ra, dec);
    if(!mount_dev) return false;
    pthread_mutex_lock(&mntdev_mutex);
    char cmd[128];
    bool ret = false;
    snprintf(cmd, 127, CMD_SETRA, rastr); // send RA
    if(!write_cmd(cmd, true)) goto retn;
    snprintf(cmd, 127, CMD_SETDEC, decstr); // send Dec
    if(!write_cmd(cmd, true)) goto retn;
    if(send_cmd_resp(CMD_SLEWRADEC, cmd, 127)){ // check error
        if(*cmd != '0'){
            WARNX("Slew error, answer: %s", cmd);
            LOGWARN("Slew error, answer: %s", cmd);
            goto retn;
        }
    }
    ret = true;
retn:
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

/**
 * @brief mount_pointAZ - point to stationary coordinates
 * @param A - azumuth (degrees): CLOCKWISE FROM NORTH!!!
 * @param Z - zenith (degrees)
 * @return false if failed
 */
bool mount_pointAZ(double A, double Z){
    char altstr[RADEC_STR_MAXLEN], azstr[RADEC_STR_MAXLEN];
    if(!normAZ(&A, &Z)){
        DBG("Wrong Z?");
        return false;
    }
    d2sDMS(A, azstr, RADEC_STR_MAXLEN);
    d2sDMS(90. - Z, altstr, RADEC_STR_MAXLEN);
    DBG("Point to stationary object, Az=%s, Alt=%s", azstr, altstr);
    if(isemulated){
        return pointAZ_emulation(A, Z);
    }
    if(!mount_dev) return false;
    pthread_mutex_lock(&mntdev_mutex);
    bool ret = false;
    char cmd[128];
    snprintf(cmd, 127, CMD_SETALT, altstr);
    if(!write_cmd(cmd, true)) goto retn;
    snprintf(cmd, 127, CMD_SETZD, azstr);
    if(!write_cmd(cmd, true)) goto retn;
    if(send_cmd_resp(CMD_GOTOAZ, cmd, 127)){ // returned not '0'
        if(*cmd != '0'){
            WARNX("Goto error, answer: %s", cmd);
            LOGWARN("Goto error, answer: %s", cmd);
            goto retn;
        }
    }
    ret = true;
retn:
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

void set_emulation_mode(){
    isemulated = true;
}

mount_status_t mount_getcoords(double *ra, double *dec){
    if(!ra || !dec) return MNT_S_ERROR;
    if(isemulated){
        get_emul_coords(ra, dec);
        DBG("Emulated coordinates: %gh, %gdeg", RAD2HRS(*ra), RAD2DEG(*dec));
        return emulation_status();
    }
    if(!mount_dev) return MNT_S_ERROR;
    char resp[64];
    mount_status_t st = MNT_S_ERROR;
    pthread_mutex_lock(&mntdev_mutex);
    if(!send_cmd_resp(CMD_GETRA, resp, 64)) goto retn;
    if(!str2coord(resp, ra)) goto retn;
    if(!send_cmd_resp(CMD_GETDEC, resp, 64)) goto retn;
    if(!str2coord(resp, dec)) goto retn;
    st = MNT_S_STATAMOUNT;
retn:
    pthread_mutex_unlock(&mntdev_mutex);
    if(st == MNT_S_STATAMOUNT) st = mount_status();
    return st;
}

mount_status_t mount_getaz(double *a, double *z){
    if(!a || !z) return MNT_S_ERROR;
    if(isemulated){
        double ra, dec;
        get_emul_coords(&ra, &dec);
        polarCrds_t P = {.dec = dec, .ra = ra};
        horizCrds_t H;
        double LST;
        get_LST(NULL, &LST);
        eq2hor(&P, &H, LST);
        *a = H.az; *z = H.zd;
        return emulation_status();
    }
    if(!mount_dev) return MNT_S_ERROR;
    char resp[64];
    mount_status_t st = MNT_S_ERROR;
    pthread_mutex_lock(&mntdev_mutex);
    if(!send_cmd_resp(CMD_GETAZIM, resp, 64)) goto retn;
    if(!str2coord(resp, a)) goto retn;
    if(!send_cmd_resp(CMD_GETALT, resp, 64)) goto retn;
    double alt;
    if(!str2coord(resp, &alt)) goto retn;
    *z = 90. - alt;
    st = MNT_S_STATAMOUNT;
retn:
    pthread_mutex_unlock(&mntdev_mutex);
    if(st == MNT_S_STATAMOUNT) st = mount_status();
    return st;
}

/**
 * @brief mount_stop - stop any moving
 * @return false if failed
 */
bool mount_stop(){
    if(isemulated){
        emulation_stop();
        return true;
    }
    if(!mount_dev) return false;
    bool ret = false;
    pthread_mutex_lock(&mntdev_mutex);
    if(write_cmd(CMD_STOP, false)) ret = true;
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

bool mount_tracking_stop(){
    if(isemulated){
        emulation_stop();
        return true;
    }
    if(!mount_dev) return false;
    bool ret = false;
    pthread_mutex_lock(&mntdev_mutex);
    if(write_cmd(CMD_TRKSTOP, false)) ret = true;
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

/**
 * @brief mount_tracking_start - start tracking from current position
 * @return false if failed
 */
bool mount_tracking_start(){
    if(isemulated){
        emul_start_tracking();
        return true;
    }
    if(!mount_dev) return false;
    bool ret = false;
    pthread_mutex_lock(&mntdev_mutex);
    write_cmd(CMD_TRKSTOP, false); // workaround from command protocol if mount was stopped with :STOP#
    if(write_cmd(CMD_TRKSTART, false)) ret = true;
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

/**
 * @brief mount_park - start parking
 * @return false if failed to run command
 */
bool mount_park(){
    if(isemulated) return pointAZ_emulation(ParkCoords.az, ParkCoords.zd);
    // TODO: set lower limit to 0deg
    return mount_pointAZ(RAD2DEG(ParkCoords.az), RAD2DEG(ParkCoords.zd));
}

/**
 * @brief mount_setParkAz - set parking azimuth
 * @param az - [-180, 360) degrees
 * @return false if az out of range
 */
bool mount_setParkAz(double az){
    if(az < 0.) az += 360.;
    if(az < 0. || az >= 360.) return false;
    ParkCoords.az = DEG2RAD(az);
    return true;
}

/**
 * @brief mount_setParkZD - set parking zenith distance
 * @param zd - [0, 90]
 * @return false if zd out of range
 */
bool mount_setParkZD(double zd){
    if(zd < 0. || zd > 90.) return false;
    ParkCoords.zd = DEG2RAD(zd);
    return true;
}

/**
 * @brief mount_getPark - get parking coordinates
 * @param c (o) - az/zd
 */
void mount_getPark(horizCrds_t *c){
    if(c) *c = ParkCoords;
}
