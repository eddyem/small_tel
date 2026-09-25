/*
 * This file is part of the Snippets project.
 * Copyright 2024 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <usefull_macros.h>
#include <weather_data.h>

#include "fitshdr.h"
#include "mount.h"
#include "server.h"
#include "stellarium.h"

// max age time of last status - 30s
#define STATUS_MAX_AGE      (30.)

// commands
#define CMD_UNIXT       "unixt"
#define CMD_STATUS      "status"
#define CMD_STOP        "stop"
#define CMD_TAGRA       "tagra"
#define CMD_TAGDEC      "tagdec"
#define CMD_TAGHA       "tagha"
#define CMD_TAGAZ       "tagaz"
#define CMD_TAGZD       "tagzd"
#define CMD_TELRA       "telra"
#define CMD_TELDEC      "teldec"
#define CMD_TELAZ       "telaz"
#define CMD_TELZD       "telzd"
#define CMD_GOTORD      "gotord"
#define CMD_GOTORH      "gotorh"
#define CMD_GOTOAZ      "gotoaz"
#define CMD_STOPTRK     "stoptrk"
#define CMD_TRACK       "track"
#define CMD_PARK        "park"
#define CMD_PARKAZ      "parkaz"
#define CMD_PARKZD      "parkzd"


// main command socket
static sl_sock_t *cmd_socket = NULL;
// socket for stellarium purposes
static int stellarium_sockfd = -1;
// sleep time (us)
static unsigned int sleept = DEFAULT_SLEEP_T;
// running flag
volatile bool isrunning = false;
// lost weather flag
static bool weatherlost = false;

// data for FITS-header (also to send user)
static fitsheader_t HDR = {0};

unsigned int server_getsleept(){ return sleept; }
bool server_setsleept(unsigned int t){
    if(t == 0 || t > MAX_SLEEP_T) return false;
    sleept = t;
    return true;
}

/////// handlers
// unixt - send to ALL clients
static sl_sock_hresult_e dtimeh(sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[32];
    snprintf(buf, 31, "%s=%.2f\n", item->key, sl_dtime());
    LOGDBG("Client %d asks time: %s", c->fd, buf);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

// statust - text format status
static sl_sock_hresult_e status(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    char buf[BUFSIZ];
    snprintf(buf, BUFSIZ-1, "%s=%s\n", item->key, mount_status_str(HDR.status));
    LOGDBG("Client %d asks status: %s", c->fd, buf);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static int parse_key_value(const char *req, double *val){
    if(!req) return 0; // is getter
    DBG("parsing of %s", req);
    double d;
    if(strchr(req, ':')){ // DD:MM:SS or HH:MM:SS
        if(!str2coord(req, &d)) return -1;
    }else if(sscanf(req, "%lf", &d) != 1) return -1; // error
    if(val) *val = d;
    return 1; // is setter
}

// setters of RA/DEC/HA/Az/ZD (TODO: fix `parse_key_value` for HH:MM:SS/DD:MM:SS/DDD.DD/HHH.HH formats!)
static sl_sock_hresult_e cmd_tagra(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){ // setter
        if(mount_setInpRA(val))  return RESULT_OK;
        return RESULT_BADVAL;
    }else{ // getter
        polarCrds_t p;
        mount_getInpCoords(&p);
        double ra_h = RAD2HRS(p.ra);
        char buf[64];
        snprintf(buf, 63, "%s=%.6f\n", item->key, ra_h);
        sl_sock_sendstrmessage(c, buf);
    }
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagdec(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        if(mount_setInpDec(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }else{
        polarCrds_t p;
        mount_getInpCoords(&p);
        double dec_d = RAD2DEG(p.dec);
        char buf[64];
        snprintf(buf, 63, "%s=%.6f\n", item->key, dec_d);
        sl_sock_sendstrmessage(c, buf);
    }
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagha(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        if(mount_setInpHA(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }else{
        polarCrds_t p;
        mount_getInpCoords(&p);
        double ha_h = RAD2HRS(p.ha);
        char buf[64];
        snprintf(buf, 63, "%s=%.6f\n", item->key, ha_h);
        sl_sock_sendstrmessage(c, buf);
    }
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagaz(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        if(mount_setInpA(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }else{
        horizCrds_t h;
        mount_getInpHor(&h);
        double az_d = RAD2DEG(h.az);
        char buf[64];
        snprintf(buf, 63, "%s=%.6f\n", item->key, az_d);
        sl_sock_sendstrmessage(c, buf);
    }
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagzd(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        if(mount_setInpZ(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }else{
        horizCrds_t h;
        mount_getInpHor(&h);
        double zd_d = RAD2DEG(h.zd);
        char buf[64];
        snprintf(buf, 63, "%s=%.6f\n", item->key, zd_d);
        sl_sock_sendstrmessage(c, buf);
    }
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_telra(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    char buf[64];
    snprintf(buf, 63, "%s=%.6f\n", item->key, RAD2HRS(HDR.polar.ra));
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_teldec(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    char buf[64];
    snprintf(buf, 63, "%s=%.6f\n", item->key, RAD2DEG(HDR.polar.dec));
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_telaz(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    char buf[64];
    snprintf(buf, 63, "%s=%.6f\n", item->key, RAD2DEG(HDR.altaz.az));
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_telzd(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    char buf[64];
    snprintf(buf, 63, "%s=%.6f\n", item->key, RAD2DEG(HDR.altaz.zd));
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_gotord(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    polarCrds_t p;
    mount_getInpCoords(&p);
    double ra_h = RAD2HRS(p.ra);
    double dec_d = RAD2DEG(p.dec);
    if(!mount_point(ra_h, dec_d)) return RESULT_FAIL;
    return RESULT_OK;
}

static sl_sock_hresult_e cmd_gotorh(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    polarCrds_t p;
    mount_getInpCoords(&p);
    sMJD_t mjd;
    if(!get_MJDt(NULL, &mjd)) return RESULT_FAIL;
    double LST;
    if(!get_LST(&mjd, &LST)) return RESULT_FAIL;
    double RA_rad = eraAnp(LST - p.ha);
    double ra_h = RAD2HRS(RA_rad);
    double dec_d = RAD2DEG(p.dec);
    if(!mount_point(ra_h, dec_d)) return RESULT_FAIL;
    return RESULT_OK;
}

static sl_sock_hresult_e cmd_gotoaz(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    horizCrds_t h;
    mount_getInpHor(&h);
    if(mount_pointAZ(RAD2DEG(h.az), RAD2DEG(h.zd))) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_stop(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(mount_stop()) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_stoptrk(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(mount_tracking_stop()) return RESULT_OK;
    return RESULT_FAIL;
}

// run tracking from current position
static sl_sock_hresult_e cmd_track(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    if(mount_tracking_start()) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_park(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(mount_park()) return RESULT_OK;
    return RESULT_FAIL;
}

// set/get parking coordinates
static sl_sock_hresult_e cmd_parkaz(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        if(mount_setParkAz(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }else{
        horizCrds_t h;
        mount_getPark(&h);
        char buf[64];
        snprintf(buf, 63, "%s=%.6f\n", item->key, RAD2DEG(h.az));
        sl_sock_sendstrmessage(c, buf);
    }
    return RESULT_SILENCE;
}
static sl_sock_hresult_e cmd_parkzd(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        if(mount_setParkZD(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }else{
        horizCrds_t h;
        mount_getPark(&h);
        char buf[64];
        snprintf(buf, 63, "%s=%.6f\n", item->key, RAD2DEG(h.zd));
        sl_sock_sendstrmessage(c, buf);
    }
    return RESULT_SILENCE;
}



//  and all handlers collection
static sl_sock_hitem_t handlers[] = {
    {cmd_gotoaz, CMD_GOTOAZ, "point telescope by input Az/ZD and stop", NULL},
    {cmd_gotord, CMD_GOTORD, "point telescope by input RA/Dec and start tracking", NULL},
    {cmd_gotorh, CMD_GOTORH, "point telescope by input RA/HA and start tracking", NULL},
    {cmd_park, CMD_PARK, "park telescope", NULL},
    {cmd_parkaz, CMD_PARKAZ, "set parking azimuth (Deg: 0 - north, 90 - east)", NULL},
    {cmd_parkzd, CMD_PARKZD, "set parking zenith distance (Deg)", NULL},
    {status, CMD_STATUS, "get mount status", NULL},
    {cmd_stop, CMD_STOP, "stop telescope", NULL},
    {cmd_tagaz, CMD_TAGAZ, "get/set target azimuth (Deg)", NULL},
    {cmd_tagdec, CMD_TAGDEC, "get/set target declination (Deg)", NULL},
    {cmd_tagha, CMD_TAGHA, "get/set target hour angle (Hrs)", NULL},
    {cmd_tagra, CMD_TAGRA, "get/set target right acsention (Hrs)", NULL},
    {cmd_tagzd, CMD_TAGZD, "get/set target zenith distance (Deg)", NULL},
    {cmd_teldec, CMD_TELDEC, "get current telescope declination (Deg)", NULL},
    {cmd_telra, CMD_TELRA, "get current telescope right acsention (Hrs)", NULL},
    {cmd_telaz, CMD_TELAZ, "get current telescope azimuth (Deg)", NULL},
    {cmd_telzd, CMD_TELZD, "get current telescope zenith distance (Deg)", NULL},
    {cmd_stoptrk, CMD_STOPTRK, "stop tracking", NULL},
    {cmd_track, CMD_TRACK, "start tracking from current position", NULL},
    {dtimeh, CMD_UNIXT, "get server's UNIX time", NULL},
    {NULL, NULL, NULL, NULL}
};

// Too much clients handler
static void toomuch(int fd){
    const char m[] = "Try later: too much clients connected\n";
    send(fd, m, sizeof(m)-1, MSG_NOSIGNAL);
    shutdown(fd, SHUT_WR);
    DBG("shutdown, wait");
    double t0 = sl_dtime();
    uint8_t buf[8];
    while(sl_dtime() - t0 < 11.){
        if(sl_canread(fd)){
            ssize_t got = read(fd, buf, 8);
            DBG("Got=%zd", got);
            if(got < 1) break;
        }
    }
    DBG("Disc after %gs", sl_dtime() - t0);
    LOGWARN("Client fd=%d tried to connect after MAX reached", fd);
}
// new connections handler: can check IP and reject client by returning FALSE
static int connected(sl_sock_t *c){
    if(c->type == SOCKT_UNIX) LOGMSG("New client fd=%d connected", c->fd);
    else LOGMSG("New client fd=%d, IP=%s connected", c->fd, c->IP);
    return TRUE;
}
// disconnected handler
static void disconnected(sl_sock_t *c){
    if(c->type == SOCKT_UNIX) LOGMSG("Disconnected client fd=%d", c->fd);
    else LOGMSG("Disconnected client fd=%d, IP=%s", c->fd, c->IP);
}

bool server_check(server_sock_t *sockt){
    sl_socktype_e type = (sockt->cmd_isunix) ? SOCKT_UNIX : SOCKT_NETLOCAL;
    cmd_socket = sl_sock_run_server(type, sockt->cmdnode, BUFSIZ, handlers);
    if(!cmd_socket){
        LOGERR("Can't start main server");
        return false;
    }
    LOGMSG("Main server started: %s", sockt->cmdnode);
    sl_sock_changemaxclients(cmd_socket, sockt->maxclients);
    sl_sock_maxclhandler(cmd_socket, toomuch);
    sl_sock_connhandler(cmd_socket, connected);
    sl_sock_dischandler(cmd_socket, disconnected);
    stellarium_sockfd = sl_sock_open(SOCKT_NET, sockt->stellport, 1, 0);
    if(stellarium_sockfd < 0){
        LOGERR("Can't start stellarium socket");
        sl_sock_delete(&cmd_socket);
        return false;
    }
    if(listen(stellarium_sockfd, sockt->maxclients) == -1){
        WARN("listen() for stellarium socket");
        LOGERR("Can't run listen() for stellarium socket");
        sl_sock_delete(&cmd_socket);
        close(stellarium_sockfd);
        return false;
    }
    int enable = 0;
    if(ioctl(stellarium_sockfd, FIONBIO, (void *)&enable) < 0){ // make socket blocking again
        WARN("ioctl()");
        LOGERR("Can't make stellarium socket blocking");
    }
    DBG("stellarium_sockfd=%d", stellarium_sockfd);
    LOGMSG("Prepared stellarium socket: %s", sockt->stellport);
    DBG("Prepared stellarium socket: %s", sockt->stellport);
    return true;
}

void server_run(){
    if(isrunning){
        LOGERR("server_run(): still running!");
        return;
    }
    if(stellarium_sockfd == -1 || !cmd_socket){
        LOGERR("server_run(): not initialized");
        if(cmd_socket) sl_sock_delete(&cmd_socket);
        ERRX("server_run(): not initialized");
    }
    if(!stellarium_start(stellarium_sockfd)){
        LOGERR("Can't start stellarium server");
        return;
    }
    isrunning = true;
    if(!mount_connect()){
        LOGWARN("Can't connect to mount, will try to reconnect later");
    }
    DBG("While");
    double tcheck = 0., treconnect = sl_dtime();
    time_t lastweathertime = 0;
    getPlaceData(&HDR.place);
    mount_get_name(HDR.mountname, MAX_HDR_STRLEN);
    bool notlogged = true;
    while(isrunning && cmd_socket && cmd_socket->connected){
        usleep(sleept);
        if(!cmd_socket->rthread){
            LOGERR("Server handlers thread is dead");
            break;
        }
        // check mount state
        double tnow = sl_dtime();
        if(tnow - tcheck >= MOUNT_CHECK_T){
            tcheck = tnow;
            if(get_weather_data(&HDR.weather) == 0){
                lastweathertime = HDR.weather.last_update;
            }else{
                WARNX("Can't get weather data");
            }
            // collect data for other fields
            HDR.status = mount_status();
            get_MJDt(NULL, &HDR.MJD);
            get_LST(&HDR.MJD, &HDR.sidtime);
            if(HDR.status != MNT_S_OFF){
                double deg1, deg2;
                if(mount_getcoords(&deg1, &deg2)){
                    HDR.polar.ra = DEG2RAD(deg1);
                    HDR.polar.dec = DEG2RAD(deg2);
                }
                if(mount_getaz(&deg1, &deg2)){
                    HDR.altaz.az = DEG2RAD(deg1);
                    HDR.altaz.zd = DEG2RAD(deg2);
                }
            }
            if(tnow - lastweathertime > MOUNT_WEATHER_ALRM){
                weatherlost = true;
                if(HDR.status == MNT_S_TRACKING || HDR.status == MNT_S_SLEWING){
                    LOGERR("Lost meteo connection -> park");
                    if(mount_park()) lastweathertime = tnow;
                }
            } else weatherlost = false;
            getDUT(&HDR.dut);
            if(HDR.status == MNT_S_OFF || !mount_getpierside(HDR.pierside, MAX_HDR_STRLEN)) *HDR.pierside = 0;
            // and write FITS-header
            wrhdr(&HDR);
            // check need of reconnection
            if(HDR.status == MNT_S_OFF){ // mount is off -> try to reconnect
                if(notlogged){
                    WARNX("Mount is OFF");
                    LOGWARN("Mount is OFF");
                    notlogged = false;
                }
                if(tnow - treconnect >= MOUNT_RECONNECT_T){
                    treconnect = tnow;
                    DBG("Try to [re]connect");
                    if(mount_connect()){
                        notlogged = true;
                        WARNX("Mount is ON");
                    }
                }
            }
            DBG("Current status: %s", mount_status_str(HDR.status));
        }
    }
    DBG("Stop mount");
    double t0 = sl_dtime();
    while(sl_dtime() - t0 < 3. && !mount_stop());
    DBG("Stop command socket");
    sl_sock_delete(&cmd_socket);
    WARNX("Server is dead");
    LOGERR("Server is dead");
    isrunning = false;
}

void server_stop(){
    isrunning = false;
}
