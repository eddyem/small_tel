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
#include <sys/time.h>
#include <usefull_macros.h>

#include "10micron_commands.h"
#include "emulation.h"
#include "mount.h"
#include "server.h" // isrunning


#define MNAME_LEN   32

static bool isemulated = false; // ==true for emulation mode

// default serial timeout, seconds
static double sertmout = 1.;

static char mount_name[MNAME_LEN] = "noname";
static sl_tty_t *mount_dev = NULL;
// device mutex, blocking only in non-local functions
static pthread_mutex_t mntdev_mutex = PTHREAD_MUTEX_INITIALIZER;
// status
static atomic_int mountstatus = MNT_S_ERROR;
// parking coordinates
/*
A="01:48:38" -> A=1.8105556
H="01:13:29" -> Z=88.7752778
*/
static horizCrds_t ParkCoords = {.az = DEG2RAD(1.8105556), .zd = DEG2RAD(88.7752778)};
// input and current target coordinates
static polarCrds_t InpCoords = {0}, // input as user give (for epoch InpMJD)
    TagCoords = {0}; // target for Jnow after command "point to input"
static horizCrds_t InpHoriz = {0};
// epoch for input coordinates
static double InpEpoch = 2000.; // J2000
// times of coords/epoch change
static double InpCTime = 0., TagTime = 0., InpHTime = 0., InpETime = 0.;

// safe strncpy
static void scpy(size_t len, char *dest, const char *src){
    size_t l = snprintf(dest, len, "%s", src);
    if(l >= len) dest[len-1] = 0;
}

// return time of modification
double mount_getInpCoords(polarCrds_t *c){
    if(c) *c = InpCoords;
    return InpCTime;
}
double mount_getTagCoords(polarCrds_t *c){
    if(c) *c = TagCoords;
    return TagTime;
}
double mount_getInpEpoch(double *epoch){
    if(epoch) *epoch = InpEpoch;
    return InpETime;
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
    if(ha < 0) ha += 24.;
    if(ha < 0. || ha >= 24.) return false;
    InpCoords.ha = HRS2RAD(ha);
    InpCoords.ra = rahaNow(InpCoords.ha);
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
    InpCoords.ha = rahaNow(InpCoords.ra);
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
bool mount_setInpEpoch(double epoch){
    if(epoch < EPOCH_MIN || epoch > EPOCH_MAX) return false;
    InpEpoch = epoch;
    InpETime = sl_dtime();
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
    if(l > MNAME_LEN-1) return false;
    strcpy(mount_name, name);
    return true;
}

void mount_get_name(char *nm, size_t l){
    if(!nm) return;
    size_t L = snprintf(nm, l, "'%s'", mount_name);
    if(L == l){
        nm[l-1] = 0;
        nm[l-2] = '\'';
    }
}

/**
 * @brief mount_set_dev - open mount device without checking that mount is alive
 * @param dev - path to device
 * @param speed - baudrate
 * @param timeout - timeout (seconds) for answer's waiting
 * @return false if failed to open device `dev`
 */
bool mount_set_dev(char *dev, int speed, double timeout){
    if(isemulated) return true;
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
    while(sl_dtime() - t0 < sertmout && !gotEOM){
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
            WARNX("no mount device?");
            return false;
        }
        if(sl_tty_write(mount_dev->comfd, cmd, strlen(cmd))){
            WARNX("Error writing request '%s'", cmd);
            return false;
        }
        while(sl_tty_read(mount_dev) > 0);
    }else if(!send_cmd_resp(cmd, buf, sizeof(buf))){
        WARNX("Error writing command '%s'", cmd);
        return false;
    }
    return true;
}

// return current mount status
mount_status_t mount_status(){
    if(isemulated) return emulation_status();
    mount_status_t curst = (mount_status_t)atomic_load(&mountstatus);
    if(curst == MNT_S_OFF) return curst; // do nothing when mount if off
    if(!mount_dev){
        atomic_store(&mountstatus, MNT_S_OFF);
        return MNT_S_OFF;
    }
    char resp[64];
    int ntry = 0;
    pthread_mutex_lock(&mntdev_mutex);
    for(; ntry < 3; ++ntry)
        if(send_cmd_resp(CMD_GETSTAT, resp, sizeof(resp))) break;
    pthread_mutex_unlock(&mntdev_mutex);
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
    for(int i = 0; i < 3 && isrunning; ++i){
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
    const int speeds[SPDBUFSZ] = {9600, 57600, 38400, 19200, 4800, 2400, 1200};
    int idx = 0;
    for(; idx < SPDBUFSZ && isrunning; ++idx){
        DBG("try speed %d", speeds[idx]);
        mount_dev->speed = speeds[idx];
        sl_tty_t *trydev = sl_tty_open(mount_dev, 1);
        if(!trydev) continue;
        if(chkconn()){
            close(mount_dev->comfd);
            break;
        }
        close(mount_dev->comfd);
    }
    mount_dev->speed = 115200;
    if(!sl_tty_open(mount_dev, 1)){
        WARN("Can't open serial @115200");
        return false;
    }
    DBG("Opened @ 115200");
    if(idx == SPDBUFSZ) return false; // device not responding
#undef SPDBUFSZ
    return true;
}

// connect to mount
bool mount_connect(){
    if(isemulated) return true;
    if(!mount_dev){
        WARNX("Mount not initialized!");
        return false;
    }
    pthread_mutex_lock(&mntdev_mutex);
    if(!chkconn() && !guess_speed()){
        pthread_mutex_unlock(&mntdev_mutex);
        return false;
    }
    bool ret = true;
    if(!write_cmd(CMD_STOP, false)) ret = false; // stop tracking after poweron
    usleep(100000);
    if(!write_cmd(CMD_HIGHPREC, false)) ret = false; // set high precision
    usleep(100000);
    char buf[64];
    snprintf(buf, 63, CMD_SETMINALT, 10);
    if(!write_cmd(buf, true)) ret = false; // set minimum altitude to 10 degrees
    pthread_mutex_unlock(&mntdev_mutex);
    if(ret){
        LOGMSG("Connected to %s@115200", mount_dev->portname);
        atomic_store(&mountstatus, MNT_S_STOPPED);
    }else LOGERR("Can't write commands to mount");
    return ret;
}

void mount_disconnect(){
    if(isemulated){
        emulation_stop();
        return;
    }
    double t0 = sl_dtime();
    while(sl_dtime() - t0 < 120. && pthread_mutex_trylock(&mntdev_mutex))
        usleep(100000);
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
    h2sHMS(ra, rastr);
    d2sDMS(dec, decstr);
    LOGDBG("try to set RA/DEC to %s/%s", rastr, decstr);
    DBG("Set RA/Decl to %s/%s", rastr, decstr);
    if(isemulated) return point_emulation(ra, dec);
    if(!mount_dev){
        WARNX("No mount device!");
        return false;
    }
    pthread_mutex_lock(&mntdev_mutex);
    char cmd[128];
    bool ret = false;
    snprintf(cmd, 127, CMD_SETRA, rastr); // send RA
    if(!write_cmd(cmd, true)) goto retn;
    snprintf(cmd, 127, CMD_SETDEC, decstr); // send Dec
    if(!write_cmd(cmd, true)) goto retn;
    if(send_cmd_resp(CMD_SLEWRADEC, cmd, sizeof(cmd))){ // check error
        if(*cmd != '0'){
            WARNX("Slew error, answer: %s", cmd);
            LOGWARN("Slew error, answer: %s", cmd);
            goto retn;
        }
    }
    ret = true;
    TagCoords.ra = HRS2RAD(ra);
    TagCoords.ha = rahaNow(TagCoords.ra);
    TagCoords.dec = DEG2RAD(dec);
    TagTime = sl_dtime();
    LOGMSG("Go to RA/Decl: %s/%s", rastr, decstr);
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
    d2sDMS(A, azstr);
    d2sDMS(90. - Z, altstr);
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
    if(send_cmd_resp(CMD_GOTOAZ, cmd, sizeof(cmd))){ // returned not '0'
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

void mount_emulation_set(){
    isemulated = true;
}

bool mount_emulation_get(){
    return isemulated;
}

static bool get_declination(double *dec){
    char resp[64];
    if(!send_cmd_resp(CMD_GETDEC, resp, sizeof(resp))) return false;
    return str2coord(resp, dec);
}

bool mount_getcoords(double *ra, double *dec){
    if(!ra || !dec) return false;
    if(isemulated){
        get_emul_coords(ra, dec);
        *ra = RAD2HRS(*ra);
        *dec = RAD2DEG(*dec);
        DBG("Emulated coordinates: %gh, %gdeg", *ra, *dec);
        return true;
    }
    if(!mount_dev) return false;
    char resp[64];
    bool ret = false;
    pthread_mutex_lock(&mntdev_mutex);
    if(!get_declination(dec)) goto retn;
    if(!send_cmd_resp(CMD_GETRA, resp, sizeof(resp))) goto retn;
    if(!str2coord(resp, ra)) goto retn;
    ret = true;
retn:
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

bool mount_getaz(double *a, double *z){
    if(!a || !z) return false;
    if(isemulated){
        double ra, dec;
        get_emul_coords(&ra, &dec);
        polarCrds_t P = {.dec = dec, .ra = ra};
        horizCrds_t H;
        double LST;
        get_LST(NULL, &LST);
        eq2hor(&P, &H, LST);
        *a = RAD2DEG(H.az); *z = RAD2DEG(H.zd);
        return true;
    }
    if(!mount_dev) return false;
    char resp[64];
    bool ret = false;
    pthread_mutex_lock(&mntdev_mutex);
    if(!send_cmd_resp(CMD_GETAZIM, resp, sizeof(resp))) goto retn;
    if(!str2coord(resp, a)) goto retn;
    if(!send_cmd_resp(CMD_GETALT, resp, sizeof(resp))) goto retn;
    double alt;
    if(!str2coord(resp, &alt)) goto retn;
    *z = 90. - alt;
    ret = true;
retn:
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}


bool mount_getinfo(mount_info_t *info){
    double ra, dec, az, zd, jd;
    char pier;
    int stat, slew;
    char resp[128];
    if(isemulated){
        DBG("Get emul info");
        mount_getaz(&az, &zd);
        mount_getcoords(&ra, &dec);
        sMJD_t mjd;
        get_MJDt(NULL, &mjd);
        jd = mjd.MJD;
        pier = 'x';
        stat = emulation_status();
        slew = 0;
    }else{
        if(!info || !mount_dev) return false;
        bool OK = false;
        pthread_mutex_lock(&mntdev_mutex);
        if(send_cmd_resp(CMD_GETINFO, resp, sizeof(resp))) OK = true;
        pthread_mutex_unlock(&mntdev_mutex);
        if(!OK) return false;
        double alt;
        if(8 != sscanf(resp, "%lf,%lf,%c,%lf,%lf,%lf,%d,%d",
                        &ra, &dec, &pier, &az, &alt, &jd, &stat, &slew)) return false;
        //LOGDBG("ra=%g, dec=%g, az=%g, alt=%g, jd=%g", ra, dec, az, alt, jd);
        zd = 90. - alt;
    }
    info->ra = HRS2RAD(ra);
    info->dec = DEG2RAD(dec);
    info->az = DEG2RAD(az);
    info->zd = DEG2RAD(zd);
    info->JD = jd;
    const char *pierside = "Unknown";
    switch(pier){
        case 'W': pierside = "West"; break;
        case 'E': pierside = "East"; break;
        default : break;
    }
    strcpy(info->pierside, pierside);
    info->status = (mount_status_t) stat;
    info->slewing = slew;
    return true;
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
    double ra, dec;
    if(!mount_getcoords(&ra, &dec)) return false;
    TagCoords.ra = HRS2RAD(ra);
    TagCoords.dec = DEG2RAD(dec);
    TagCoords.ha = rahaNow(TagCoords.ra);
    TagTime = sl_dtime();
    mount_setInpEpoch(-1.); // Jnow
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
    if(isemulated) return pointAZ_emulation(RAD2DEG(ParkCoords.az), RAD2DEG(ParkCoords.zd));
    char buf[64];
    snprintf(buf, 63, CMD_SETMINALT, 0);
    pthread_mutex_lock(&mntdev_mutex);
    bool ret = write_cmd(buf, true);
    pthread_mutex_unlock(&mntdev_mutex);
    if(!ret) return false;
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

/**
 * @brief mount_getpierside - get pier-side of telescope
 * @param Ps (o) - side
 * @param len - length of `Ps`
 * @return false if failed
 */
bool mount_getpierside(char *Ps, size_t len){
    if(isemulated){
        scpy(len, Ps, "Emulation");
        return true;
    }
    pthread_mutex_lock(&mntdev_mutex);
    bool ret = send_cmd_resp(CMD_GETPS, Ps, len);
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

/**
 * @brief mount_corrdata - correct mount time & weather data
 * @param w - new data
 * @return false if failed to connect
 */
bool mount_corrdata(weather_data_t *w){
    if(isemulated) return true;
    if(!w) return false;
    char buf[128];
    buf[127] = 0;
    bool ret = true; // returning value

    DBG("Refresh datetime");
    pthread_mutex_lock(&mntdev_mutex);

    double curt = sl_dtime() + FRAC_T_TODO;
    time_t seconds = (int) curt;
    double frac = curt - seconds;
    struct tm tm_buf;
    struct tm *stm = localtime_r(&seconds, &tm_buf);
    snprintf(buf, 127, CMD_SETTIME, 1900+stm->tm_year, stm->tm_mon+1, stm->tm_mday,
             stm->tm_hour, stm->tm_min, (double)stm->tm_sec + frac);
    DBG("write: %s", buf);
    if(!write_cmd(buf, true)){
        WARNX("Can't write current date/time");
        LOGWARN("Can't set system time");
        ret = false;
    }else LOGMSG("Set system time by command %s", buf);
    // set refraction model data
    snprintf(buf, 127, CMD_SETPRESSURE, w->pressure*1013./760.);
    if(!write_cmd(buf, true)){
        LOGWARN("Can't set pressure data of refraction model");
        ret = false;
    }else LOGMSG("Correct pressure to %gmmHg", w->pressure);
    snprintf(buf, 64, CMD_SETTEMPER, w->exttemp);
    if(!write_cmd(buf, true)){
        LOGWARN("Can't set temperature data of refraction model");
        ret = false;
    }else LOGMSG("Correct temperature to %g", w->exttemp);
    if(!write_cmd(CMD_REFCORR_ON, true)) ret = false;
    if(!write_cmd(CMD_DUALTRK, true)) ret = false;
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

/**
 * @brief mount_shutdown - turn off mount
 * @return false if failed
 */
bool mount_shutdown(){
    if(isemulated) return false;
    pthread_mutex_lock(&mntdev_mutex);
    bool ret = write_cmd(CMD_SHUTDOWN, true);
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

/**
 * @brief mount_rawcmd - send raw command `cmd` to mount
 * @param cmd - raw command
 * @param ans (o) - answer text
 * @param anslen - length of `ans` buffer
 * @return false if failed
 */
bool mount_rawcmd(const char *cmd, char *ans, size_t anslen){
    if(!cmd || isemulated) return false;
    pthread_mutex_lock(&mntdev_mutex);
    //LOGDBG("Send %s", cmd);
    bool ret = send_cmd_resp(cmd, ans, anslen);
    //LOGDBG("Ans: %s", ret ? "true" : "false");
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}


/**** Coordinate correction (useful for guiding etc) ****/
/**
 * @brief get_guide_rate - get current guiding rate
 * @return fraction of sidereal rate
 */
static double get_guide_rate(){
    char resp[32];
    if(send_cmd_resp(CMD_GETGUIDERATE, resp, sizeof(resp))){
        double rate = atof(resp);
        //LOGDBG("get guide rate: %g", rate);
        if(rate > 0.0 && rate <= SIDEREAL_RATE){
            return rate;
        }
    }
    return DEFAULT_GUIDE_RATE;
}

/**
 * @brief send_pulse - common funtion to send correction pulse
 * @param dir_cmd - direction like "Me" etc.
 * @param total_ms - pulse length
 * @return false if failed
 */
static bool send_pulse(const char *dir_cmd, int total_ms){
    char cmd[16];
    snprintf(cmd, sizeof(cmd), CMD_CORRECT, dir_cmd, total_ms);
    LOGDBG("Correct by command %s", cmd);
    return write_cmd(cmd, false); // these commands have no answer
}

/**
 * @brief correct_ra - correct RA by `arcsec` arcseconds
 * @param arcsec - angle to correct
 * @return false if failed
 */
bool correct_ra(double arcsec){
    if(fabs(arcsec) < 1e-6) return false; // near pole there's may be...
    if(isemulated) return emul_corra(arcsec);
    bool ret = false;
    pthread_mutex_lock(&mntdev_mutex);
    double guide_rate = get_guide_rate();
    // recalculate by plate coordinates
    double dec_deg;
    if(!get_declination(&dec_deg)){
        LOGDBG("Can't get dec");
        goto rtn;
    }
    double cos_dec = cos(DEG2RAD(dec_deg));
    if(fabs(cos_dec) < 1e-6) cos_dec = 1e-6; // avoid dividing by zero
    // calculate guiding duration
    double duration_ms = (fabs(arcsec) / cos_dec) / (guide_rate) * 1000.0;
    //LOGDBG("arcsec=%g, cosdec=%g, gr=%g, dms=%g", arcsec, cos_dec, guide_rate, duration_ms);
    int total_ms = (int)round(duration_ms);
    if(total_ms > MAX_PULSE_MS){
        LOGDBG("duration %d too high", total_ms);
        goto rtn;
    }
    const char *dir = (arcsec > 0) ? CORR_EAST : CORR_WEST;
    ret = send_pulse(dir, total_ms);
rtn:
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}

/**
 * @brief correct_dec - make correction for declination
 * @param arcsec - angle to correct
 * @return false if failed
 */
bool correct_dec(double arcsec){
    if(fabs(arcsec) < MIN_CORR_ANGLE) return false;
    if(isemulated) return emul_cordec(arcsec);
    bool ret = false;
    pthread_mutex_lock(&mntdev_mutex);
    double guide_rate = get_guide_rate();
    double duration_ms = fabs(arcsec) / (guide_rate) * 1000.0;
    int total_ms = (int)round(duration_ms);
    if(total_ms > MAX_PULSE_MS) goto rtn;
    const char *dir = (arcsec > 0) ? CORR_NORTH : CORR_SOUTH;
    ret = send_pulse(dir, total_ms);
rtn:
    pthread_mutex_unlock(&mntdev_mutex);
    return ret;
}
