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

#include "mount.h"
#include "server.h"
#include "stellarium.h"

// max age time of last status - 30s
#define STATUS_MAX_AGE      (30.)

// commands
#define CMD_CORDEC      "cordec"
#define CMD_CORRA       "corra"
#define CMD_DUT1        "dut1"
#define CMD_GOTOAZ      "gotoaz"
#define CMD_GOTORD      "gotord"
#define CMD_GOTORH      "gotorh"
#define CMD_LST         "lst"
#define CMD_PARKAZ      "parkaz"
#define CMD_PARK        "park"
#define CMD_PARKZD      "parkzd"
#define CMD_PLACE       "place"
#define CMD_POLARX      "polarx"
#define CMD_POLARY      "polary"
#define CMD_RAW         "raw"
#define CMD_SHUTDOWN    "shutdown"
#define CMD_STATUS      "status"
#define CMD_STOP        "stop"
#define CMD_STOPTRK     "stoptrk"
#define CMD_TAGAZ       "tagaz"
#define CMD_TAGDEC      "tagdec"
#define CMD_TAGEPOCH    "tagepoch"
#define CMD_TAGHA       "tagha"
#define CMD_TAGRA       "tagra"
#define CMD_TAGZD       "tagzd"
#define CMD_TELAZ       "telaz"
#define CMD_TELDEC      "teldec"
#define CMD_TELHA       "telha"
#define CMD_TELRA       "telra"
#define CMD_TELZD       "telzd"
#define CMD_TRACK       "track"
#define CMD_UNIXT       "unixt"
#define CMD_UPDATE      "update"
#define CMD_FORMAT      "format"


// main command socket
static sl_sock_t *cmd_socket = NULL;
// socket for stellarium purposes
static int stellarium_sockfd = -1;
// sleep time (us)
static unsigned int sleept = DEFAULT_SLEEP_T;
// running flag
volatile bool isrunning = true;
// lost weather flag
static bool weatherlost = false;

// make weather/time correction even in tracking state
static bool needcorr = false;

// data for FITS-header (also to send user)
static fitsheader_t HDR = {0};
static pthread_mutex_t headermutex = PTHREAD_MUTEX_INITIALIZER;

unsigned int server_getsleept(){ return sleept; }
bool server_setsleept(unsigned int t){
    if(t == 0 || t > MAX_SLEEP_T) return false;
    sleept = t;
    return true;
}

// print hours and angles in user's format
static void print_hour(sl_sock_t *c, const char *key, double radians){
    int format = (intptr_t)c->data;
    char buf[64], valbuf[RADEC_STR_MAXLEN];
    size_t L = 0;
    if(0 == format){ // default: double number
        L = snprintf(buf, 63, "%s=%.8f\n", key, RAD2HRS(radians));
    }else{
        r2sHMS(radians, valbuf);
        L = snprintf(buf, 63, "%s=%s\n", key, valbuf);
    }
    if(L > 62) buf[63] = 0;
    sl_sock_sendstrmessage(c, buf);
}

static void print_deg(sl_sock_t *c, const char *key, double radians){
    int format = (intptr_t)c->data;
    char buf[64], valbuf[RADEC_STR_MAXLEN];
    size_t L = 0;
    if(0 == format){ // default: double number
        L = snprintf(buf, 63, "%s=%.8f\n", key, RAD2DEG(radians));
    }else{
        r2sDMS(radians, valbuf);
        L = snprintf(buf, 63, "%s=%s\n", key, valbuf);
    }
    if(L > 62) buf[63] = 0;
    sl_sock_sendstrmessage(c, buf);
}

/////// handlers
// unixt - send to ALL clients
static sl_sock_hresult_e cmd_dtimeh(sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[32];
    snprintf(buf, 31, "%s=%.2f\n", item->key, sl_dtime());
    LOGDBG("Client %d asks time: %s", c->fd, buf);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

// statust - text format status
static sl_sock_hresult_e cmd_status(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
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
    if(!str2coord(req, &d)) return -1;
    if(val) *val = d;
    return 1; // is setter
}

// setters of RA/DEC/HA/Az/ZD
static sl_sock_hresult_e cmd_tagra(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){ // setter
        LOGDBG("Client %d set tagra: %g", c->fd, val);
        if(mount_setInpRA(val))  return RESULT_OK;
        return RESULT_BADVAL;
    } // getter
    polarCrds_t p;
    mount_getInpCoords(&p);
    print_hour(c, item->key, p.ra);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagdec(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        LOGDBG("Client %d set tagdec: %g", c->fd, val);
        if(mount_setInpDec(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    polarCrds_t p;
    mount_getInpCoords(&p);
    print_deg(c, item->key, p.dec);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagha(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        LOGDBG("Client %d set tagha: %g", c->fd, val);
        if(mount_setInpHA(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    polarCrds_t p;
    mount_getInpCoords(&p);
    print_hour(c, item->key, p.ha);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagaz(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        LOGDBG("Client %d set tagaz: %g", c->fd, val);
        if(mount_setInpA(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    horizCrds_t h;
    mount_getInpHor(&h);
    print_deg(c, item->key, h.az);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagzd(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        LOGDBG("Client %d set tagzd: %g", c->fd, val);
        if(mount_setInpZ(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    horizCrds_t h;
    mount_getInpHor(&h);
    print_deg(c, item->key, h.zd);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_lst(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    print_hour(c, item->key, HDR.sidtime);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_telra(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    print_hour(c, item->key, HDR.polar.ra);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_telha(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    print_hour(c, item->key, HDR.polar.ha);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_teldec(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    print_deg(c, item->key, HDR.polar.dec);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_telaz(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    print_deg(c, item->key, HDR.altaz.az);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_telzd(sl_sock_t *c, sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    print_deg(c, item->key, HDR.altaz.zd);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_gotord(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    polarCrds_t px, pnow;
    mount_getInpCoords(&px);
    double Epoch;
    mount_getInpEpoch(&Epoch);
    if(Epoch > 0.){
        LOGDBG("Convert ra=%ghrs, dec=%gdeg from epoch=%g", RAD2HRS(px.ra), RAD2DEG(px.dec), Epoch);
        if(!JXtoJnow(&px, &pnow, Epoch)) return RESULT_FAIL;
    }else pnow = px;
    double ra_h = RAD2HRS(pnow.ra);
    double dec_d = RAD2DEG(pnow.dec);
    LOGMSG("Client %d asks to point ra=%ghrs, dec=%gdeg", c->fd, ra_h, dec_d);
    if(!mount_point(ra_h, dec_d)) return RESULT_FAIL;
    return RESULT_OK;
}

static sl_sock_hresult_e cmd_gotorh(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    polarCrds_t p;
    mount_getInpCoords(&p);
    double RA_rad = rahaNow(p.ha); // re-convert (as time could have passed)
    double ra_h = RAD2HRS(RA_rad);
    double dec_d = RAD2DEG(p.dec);
    LOGDBG("Client %d asks to point to HA/Dec", c->fd);
    if(!mount_point(ra_h, dec_d)) return RESULT_FAIL;
    return RESULT_OK;
}

static sl_sock_hresult_e cmd_gotoaz(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    horizCrds_t h;
    mount_getInpHor(&h);
    LOGDBG("Client %d asks to point to A/Z", c->fd);
    if(mount_pointAZ(RAD2DEG(h.az), RAD2DEG(h.zd))) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_stop(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    LOGDBG("Client %d asks to stop", c->fd);
    if(mount_stop()) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_stoptrk(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    LOGDBG("Client %d asks to stop tracking", c->fd);
    if(mount_tracking_stop()) return RESULT_OK;
    return RESULT_FAIL;
}

// run tracking from current position
static sl_sock_hresult_e cmd_track(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(weatherlost || HDR.status == MNT_S_ERROR) return RESULT_FAIL;
    LOGDBG("Client %d asks to start tracking", c->fd);
    if(mount_tracking_start()) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_park(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    LOGDBG("Client %d asks to park", c->fd);
    if(mount_park()) return RESULT_OK;
    return RESULT_FAIL;
}

// set/get parking coordinates
static sl_sock_hresult_e cmd_parkaz(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        LOGDBG("Client %d set parking Az to %g", c->fd, val);
        if(mount_setParkAz(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    horizCrds_t h;
    mount_getPark(&h);
    print_deg(c, item->key, h.az);
    return RESULT_SILENCE;
}
static sl_sock_hresult_e cmd_parkzd(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){
        LOGDBG("Client %d set parking ZD to %g", c->fd, val);
        if(mount_setParkZD(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    horizCrds_t h;
    mount_getPark(&h);
    print_deg(c, item->key, h.zd);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_tagepoch(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){ // setter
        LOGDBG("Client %d set target epoch %g", res);
        if(mount_setInpEpoch(val)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    mount_getInpEpoch(&val);
    char buf[64];
    snprintf(buf, 63, "%s=%.6f\n", item->key, val);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_dut1(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    almDut_t curval;
    getDUT(&curval);
    if(res > 0){
        LOGDBG("Client %d set dut1 %g", c->fd, val);
        curval.DUT1 = val;
        if(setDUT(&curval)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    char buf[64];
    snprintf(buf, 63, "%s=%.3f\n", item->key, curval.DUT1);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_polarx(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    almDut_t curval;
    getDUT(&curval);
    if(res > 0){
        LOGDBG("Client %d set polarx %g", c->fd, val);
        curval.px = val;
        if(setDUT(&curval)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    char buf[64];
    snprintf(buf, 63, "%s=%.3f\n", item->key, curval.px);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_polary(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    almDut_t curval;
    getDUT(&curval);
    if(res > 0){
        LOGDBG("Client %d set polary %g", c->fd, val);
        curval.py = val;
        if(setDUT(&curval)) return RESULT_OK;
        return RESULT_BADVAL;
    }
    char buf[64];
    snprintf(buf, 63, "%s=%.3f\n", item->key, curval.py);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_place(sl_sock_t *c, sl_sock_hitem_t _U_ *item, const char _U_ *req){
    char buf[32];
    placeData_t place;
    getPlaceData(&place);
    print_deg(c, "latitude", place.slat);
    print_deg(c, "longitude", place.slong);
    snprintf(buf, 31, "altitude=%.7f\n", place.salt);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

// parsing of raw command (for `cmd_raw` and `unknown_command`)
static sl_sock_hresult_e rawcommand(sl_sock_t *c, const char *req){
    char buf[128];
    if(!mount_rawcmd(req, buf, 126)){
        LOGMSG("Client %d asks for raw command '%s'; got no answer or error", c->fd, req);
        return RESULT_FAIL;
    }
    size_t L = strlen(buf);
    if(L > 126){ L = 126; buf[126] = 0; }
    LOGMSG("Client %d asks for raw command '%s'; answer: '%s'", c->fd, req, buf);
    buf[L] = '\n';
    buf[L + 1] = 0;
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

static sl_sock_hresult_e cmd_raw(sl_sock_t *c, sl_sock_hitem_t _U_ *item, const char *req){
    char cmdbuf[32], *cmdptr = cmdbuf;
    if(!req || !*req) return RESULT_BADVAL;
    size_t L = strlen(req);
    if(L < 1 || L > 29) return RESULT_BADVAL;
    if(*req != ':') *cmdptr++ = ':';
    if(req[L-1] != '#') sprintf(cmdptr, "%s#", req);
    else strcpy(cmdptr, req);
    return rawcommand(c, cmdbuf);
}

static sl_sock_hresult_e cmd_shutdown(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    static int shtdwnkey = -1;
    static time_t keytime = 0;
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    time_t tnow = time(NULL);
    if(tnow - keytime > SHTDWN_KEY_TMOUT) keytime = 0;
    if(res == 0){ // generate new shutdown key or show old - depending on keytime
        if(!keytime){
            keytime = tnow;
            srand(tnow);
            shtdwnkey = rand();
            LOGDBG("Generated new shutdown key: %d", shtdwnkey);
        }
        char buf[64];
        snprintf(buf, 63, "%s=%d\n", item->key, shtdwnkey);
        sl_sock_sendstrmessage(c, buf);
        return RESULT_SILENCE;
    }
    if((int)val != shtdwnkey){
        LOGWARN("User tryes to run shutdown with wrong key");
        return RESULT_BADVAL;
    }
    LOGWARN("User give command \"shutdown\"");
    if(!mount_shutdown()) return RESULT_FAIL;
    return RESULT_OK;
}

static sl_sock_hresult_e cmd_cordec(sl_sock_t *c, sl_sock_hitem_t _U_ *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res <= 0) return RESULT_BADVAL;
    LOGDBG("Client %d ask to correct Dec by %g''", c->fd, val);
    if(correct_dec(val)) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_corra(sl_sock_t *c, sl_sock_hitem_t _U_ *item, const char *req){
    double val;
    int res = parse_key_value(req, &val);
    if(res <= 0) return RESULT_BADVAL;
    LOGDBG("Client %d ask to correct RA by %g''", c->fd, val);
    if(correct_ra(val)) return RESULT_OK;
    return RESULT_FAIL;
}

static sl_sock_hresult_e cmd_update(sl_sock_t _U_ *c, sl_sock_hitem_t _U_ *item, const char _U_ *req){
    if(HDR.status == MNT_S_OFF) return RESULT_FAIL;
    needcorr = true;
    return RESULT_OK;
}

static sl_sock_hresult_e cmd_format(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    double val;
    char buf[32];
    int res = parse_key_value(req, &val);
    if(res < 0) return RESULT_BADVAL;
    if(res > 0){ // set format: 0 - double, 1 - string
        c->data = (val < 0.5) ? (void*)(intptr_t)0 : (void*)(intptr_t)1;
    }
    snprintf(buf, 31, "%s=%" PRIdPTR "\n", item->key, (intptr_t)c->data);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

// default handler: if message starts from ':' and ends by '#' treat it as raw command
static sl_sock_hresult_e unknown_command(struct sl_sock *c, const char *str){
    if(!str || !*str) return RESULT_BADKEY;
    size_t L = strlen(str);
    if(*str != ':' || str[L-1] != '#') return RESULT_BADKEY;
    return rawcommand(c, str);
}


//  and all handlers collection
static sl_sock_hitem_t handlers[] = {
    {sl_comment, NULL, "    Moving", NULL},
    {cmd_gotoaz, CMD_GOTOAZ, "point telescope by input Az/ZD and stop", NULL},
    {cmd_gotord, CMD_GOTORD, "point telescope by input RA/Dec and start tracking", NULL},
    {cmd_gotorh, CMD_GOTORH, "point telescope by input RA/HA and start tracking", NULL},
    {cmd_park, CMD_PARK, "park telescope", NULL},
    {cmd_stop, CMD_STOP, "stop telescope", NULL},
    {cmd_stoptrk, CMD_STOPTRK, "stop tracking", NULL},
    {cmd_track, CMD_TRACK, "start tracking from current position", NULL},

    {sl_comment, NULL, "    Get coordinates", NULL},
    {cmd_telaz, CMD_TELAZ, "get current telescope azimuth (Deg)", NULL},
    {cmd_teldec, CMD_TELDEC, "get current telescope declination (Deg)", NULL},
    {cmd_telha, CMD_TELHA, "get current telescope hour angle (Hrs)", NULL},
    {cmd_telra, CMD_TELRA, "get current telescope right acsention (Hrs)", NULL},
    {cmd_telzd, CMD_TELZD, "get current telescope zenith distance (Deg)", NULL},

    {sl_comment, NULL, "    Set coordinates", NULL},
    {cmd_parkaz, CMD_PARKAZ, "set parking azimuth (Deg: 0 - north, 90 - east)", NULL},
    {cmd_parkzd, CMD_PARKZD, "set parking zenith distance (Deg)", NULL},
    {cmd_tagaz, CMD_TAGAZ, "get/set target azimuth (Deg)", NULL},
    {cmd_tagdec, CMD_TAGDEC, "get/set target declination (Deg)", NULL},
    {cmd_tagepoch, CMD_TAGEPOCH, "get/set target epoch (-1 for Jnow), like 2050.", NULL},
    {cmd_tagha, CMD_TAGHA, "get/set target hour angle (Hrs)", NULL},
    {cmd_tagra, CMD_TAGRA, "get/set target right acsention (Hrs)", NULL},
    {cmd_tagzd, CMD_TAGZD, "get/set target zenith distance (Deg)", NULL},

    {sl_comment, NULL, "    Correction", NULL},
    {cmd_cordec, CMD_CORDEC, "correct Dec by x arcseconds", NULL},
    {cmd_corra, CMD_CORRA, "correct RA by x arcseconds", NULL},
    {cmd_update, CMD_UPDATE, "update time/weather data to mount", NULL},

    {sl_comment, NULL, "    Mount specific", NULL},
    {cmd_raw, CMD_RAW, "send raw command to mount", NULL},
    {cmd_shutdown, CMD_SHUTDOWN, "shut down mount power", NULL},
    {cmd_status, CMD_STATUS, "get mount status", NULL},

    {sl_comment, NULL, "    Miscellaneous", NULL},
    {cmd_dtimeh, CMD_UNIXT, "get server's UNIX time", NULL},
    {cmd_dut1, CMD_DUT1, "set DUT1 (s)", NULL},
    {cmd_format, CMD_FORMAT, "change output angles format: floating (0) or string (1)", NULL},
    {cmd_lst, CMD_LST, "get current local sidereal time", NULL},
    {cmd_place, CMD_PLACE, "get place data", NULL},
    {cmd_polarx, CMD_POLARX, "set polar X value (m)", NULL},
    {cmd_polary, CMD_POLARY, "set polar Y value (m)", NULL},

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
    sl_sock_defmsghandler(cmd_socket, unknown_command);
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

    double tcheck = sl_dtime(), treconnect = 0.;
    bool notlogged = true;
    double lastweathertime = 0; // last weather checking time
    double lastcorr = 0.; // last time of time/weather corrections sent to mount

    getPlaceData(&HDR.place);
    mount_get_name(HDR.mountname, MAX_HDR_STRLEN);
    bool isemulated = mount_emulation_get();
    DBG("While");
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
            if(!isemulated){
                if(get_weather_data(&HDR.weather) == 0){ // for airmass
                    lastweathertime = HDR.weather.last_update;
                }else{
                    WARNX("Can't get weather data");
                }
            }
            // collect data for other fields
            pthread_mutex_lock(&headermutex);
            HDR.status = mount_status();
            get_MJDt(NULL, &HDR.MJD);
            get_LST(&HDR.MJD, &HDR.sidtime);
            if(HDR.status != MNT_S_OFF){
                mount_info_t info;
                if(mount_getinfo(&info)){
                    LOGDBG("Got info; JDtel-JDcomp=%gs", 86400.*(info.JD - ERFA_DJM0 - HDR.MJD.MJD));
                    HDR.altaz.az = info.az;
                    HDR.altaz.zd = info.zd;
                    HDR.polar.dec = info.dec;
                    HDR.polar.ra = info.ra;
                    HDR.polar.ha = eraAnp(HDR.sidtime - HDR.polar.ra);
                    strcpy(HDR.pierside, info.pierside);
                }else LOGWARN("Can't get mount info");
                /*
                double deg1, deg2;
                DBG("Take coordinates");
                if(mount_getcoords(&deg1, &deg2)){
                    HDR.polar.ra = HRS2RAD(deg1);
                    HDR.polar.dec = DEG2RAD(deg2);
                    HDR.polar.ha = eraAnp(HDR.sidtime - HDR.polar.ra);
                }
                if(mount_getaz(&deg1, &deg2)){
                    HDR.altaz.az = DEG2RAD(deg1);
                    HDR.altaz.zd = DEG2RAD(deg2);
                }
                */
                if(!isemulated){
                    if(tnow - lastweathertime > MOUNT_WEATHER_ALRM){
                        weatherlost = true;
                        if(HDR.status == MNT_S_TRACKING || HDR.status == MNT_S_SLEWING){
                            LOGERR("Lost meteo connection -> park");
                            if(mount_park()) lastweathertime = tnow;
                        }
                    } else {
                        weatherlost = false;
                        if(tnow - lastweathertime < (int)(1. + MOUNT_CHECK_T * 10.)){ // weather data is good
                            // make correction once per hour, but only in non-tracking state
                            if(needcorr || ((HDR.status != MNT_S_TRACKING) && tnow - lastcorr > CORRECTIONS_TIMEDIFF)){
                                if(mount_corrdata(&HDR.weather)){
                                    needcorr = false;
                                    lastcorr = tnow;
                                }
                            }
                        }else WARNX("bad weather; curtime - HDR->weather.last_update = %g", tnow - lastweathertime);
                    }
                }
            }
            getDUT(&HDR.dut);
            //if(HDR.status == MNT_S_OFF || !mount_getpierside(HDR.pierside, MAX_HDR_STRLEN)) *HDR.pierside = 0;
            pthread_mutex_unlock(&headermutex);
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
                    }else WARNX("Still can't connect");
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

void server_getheader(fitsheader_t *fh){
    if(!fh) return;
    pthread_mutex_lock(&headermutex);
    *fh = HDR;
    pthread_mutex_unlock(&headermutex);
}
