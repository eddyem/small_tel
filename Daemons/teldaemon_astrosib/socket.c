/**
 * This file is part of the teldaemon project.
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

#include <stdatomic.h>
#include <inttypes.h>
#include <pthread.h>
#include <string.h>

#include "socket.h"
#include "term.h"

typedef enum{
    CMD_OPEN,
    CMD_CLOSE,
    CMD_FOCSTOP,
    CMD_COOLERON,
    CMD_COOLEROFF,
    CMD_NONE
} tel_commands_t;



typedef struct{
    tel_commands_t cmd; // deferred command
    int focuserpos;     // focuser position
    char *status;       // device status
    int statlen;        // size of `status` buffer
    double stattime;    // time of last status
    int cooler;         // cooler's status
    double mirrortemp;  // T mirror, degC
    double ambienttemp; // T ambient, degC
    double temptime;    // measurement time
    pthread_mutex_t mutex;
} tel_t;

static tel_t telescope = {0};

// external signal "deny/allow"
static atomic_bool ForbidObservations = 0; // ==1 if all is forbidden -> close dome and not allow to open

static sl_sock_t *locksock = NULL; // local server socket
static sl_ringbuffer_t *rb = NULL; // incoming serial data

// args of absolute/relative focus move commands
static sl_sock_int_t Absmove = {0};
static sl_sock_int_t Relmove = {0};

void stopserver(){
    if(locksock) sl_sock_delete(&locksock);
    if(rb) sl_RB_delete(&rb);
}

// send "measuret=..."
static void sendtmeasured(sl_sock_t *client, double t){
    char buf[256];
    snprintf(buf, 255, "measuret=%.3f\n", t);
    sl_sock_sendstrmessage(client, buf);
}

static sl_sock_hresult_e dtimeh(sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[32];
    snprintf(buf, 31, "UNIXT=%.3f\n", sl_dtime());
    sl_sock_sendstrmessage(client, buf);
    return RESULT_SILENCE;
}

#define CHKALLOWED() do{if(ForbidObservations){pthread_mutex_unlock(&telescope.mutex); return RESULT_FAIL;}}while(0)

static sl_sock_hresult_e openh(_U_ sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    pthread_mutex_lock(&telescope.mutex);
    CHKALLOWED();
    telescope.cmd = CMD_OPEN;
    pthread_mutex_unlock(&telescope.mutex);
    return RESULT_OK;
}

static sl_sock_hresult_e closeh(_U_ sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    pthread_mutex_lock(&telescope.mutex);
    telescope.cmd = CMD_CLOSE;
    pthread_mutex_unlock(&telescope.mutex);
    return RESULT_OK;
}

static sl_sock_hresult_e fstoph(_U_ sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    pthread_mutex_lock(&telescope.mutex);
    CHKALLOWED();
    telescope.cmd = CMD_FOCSTOP;
    pthread_mutex_unlock(&telescope.mutex);
    return RESULT_OK;
}

static sl_sock_hresult_e statush(sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[256];
    double t = NAN;
    pthread_mutex_lock(&telescope.mutex);
    if(!*telescope.status || sl_dtime() - telescope.stattime > 3.*T_INTERVAL) snprintf(buf, 255, "%s=unknown\n", item->key);
    else{
        snprintf(buf, 255, "%s=%s\n", item->key, telescope.status);
        t = telescope.stattime;
    }
    sl_sock_sendstrmessage(client, buf);
    if(!isnan(t)) sendtmeasured(client, t);
    snprintf(buf, 255, "mirrortemp=%.1f\n", telescope.mirrortemp);
    sl_sock_sendstrmessage(client, buf);
    snprintf(buf, 255, "ambienttemp=%.1f\n", telescope.ambienttemp);
    sl_sock_sendstrmessage(client, buf);
    pthread_mutex_unlock(&telescope.mutex);
    if(ForbidObservations) sl_sock_sendstrmessage(client, "FORBIDDEN\n");
    return RESULT_SILENCE;
}

static sl_sock_hresult_e coolerh(sl_sock_t *client, sl_sock_hitem_t *item, const char *req){
    char buf[256];
    if(req){ // getter
        int onoff;
        if(!sl_str2i(&onoff, req)) return RESULT_BADVAL;
        pthread_mutex_lock(&telescope.mutex);
        if(onoff) telescope.cmd = CMD_COOLERON;
        else telescope.cmd = CMD_COOLEROFF;
        pthread_mutex_unlock(&telescope.mutex);
        return RESULT_OK;
    }
    // getter
    pthread_mutex_lock(&telescope.mutex);
    snprintf(buf, 255, "%s=%d\n", item->key, telescope.cooler);
    pthread_mutex_unlock(&telescope.mutex);
    sl_sock_sendstrmessage(client, buf);
    return RESULT_SILENCE;
}

// focuser getter
static sl_sock_hresult_e foch(sl_sock_t *client, sl_sock_hitem_t *item, _U_ const char *req){
    char buf[256];
    pthread_mutex_lock(&telescope.mutex);
    snprintf(buf, 255, "%s=%d\n", item->key, telescope.focuserpos);
    pthread_mutex_unlock(&telescope.mutex);
    sl_sock_sendstrmessage(client, buf);
    return RESULT_SILENCE;
}

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
// new connections handler (return FALSE to reject client)
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
// default (unknown key) handler
static sl_sock_hresult_e defhandler(struct sl_sock *s, const char *str){
    if(!s || !str) return RESULT_FAIL;
    sl_sock_sendstrmessage(s, "You entered wrong command:\n```\n");
    sl_sock_sendstrmessage(s, str);
    sl_sock_sendstrmessage(s, "\n```\nTry \"help\"\n");
    return RESULT_SILENCE;
}

static sl_sock_hitem_t handlers[] = {
#if 0
    {sl_sock_inthandler, "int", "set/get integer flag", (void*)&iflag},
    {sl_sock_dblhandler, "dbl", "set/get double flag", (void*)&dflag},
    {sl_sock_strhandler, "str", "set/get string variable", (void*)&sflag},
    {keyparhandler, "flags", "set/get bit flags as whole (flags=val) or by bits (flags[bit]=val)", (void*)&kph_number},
    {show, "show", "show current flags @ server console", NULL},
#endif
    {sl_sock_inthandler, "focrel", "relative focus move", (void*)&Relmove},
    {sl_sock_inthandler, "focabs", "absolute focus move", (void*)&Absmove},
    {foch, "focpos", "current focuser position", NULL},
    {openh, "open", "open shutters", NULL},
    {closeh, "close", "close shutters", NULL},
    {statush, "status", "get shutters' status and temperatures", NULL},
    {fstoph, "focstop", "stop focuser moving", NULL},
    {coolerh, "cooler", "get/set cooler status", NULL},
    {dtimeh, "dtime", "get server's UNIX time for all clients connected", NULL},
    {NULL, NULL, NULL, NULL}
};

static void serial_parser(){
    char line[256];
    int l = 0;
    do{
        l = read_term(line, 256);
        if(l > 0){
            if(l != (int)sl_RB_write(rb, (uint8_t*) line, l)) break;
        }
    }while(l > 0);

    pthread_mutex_lock(&telescope.mutex); // prepare user buffers
    // read ringbuffer, run parser and change buffers in `telescope`
    while((l = sl_RB_readto(rb, '\r', (uint8_t*)line, sizeof(line))) > 0){
        line[l-1] = 0; // substitute '\r' with 0
        DBG("IN: %s", line);
        if(strncmp(line, TXT_ANS_STATUS, strlen(TXT_ANS_STATUS)) == 0){
            DBG("Got status ans");
            telescope.stattime = sl_dtime();
            char *s = line + strlen(TXT_ANS_STATUS);
            if(strncmp(s, "0,0,0,0,0", 9) == 0) snprintf(telescope.status, telescope.statlen, "closed");
            else if(strncmp(s, "1,1,1,1,1", 9) == 0) snprintf(telescope.status, telescope.statlen, "opened");
            else snprintf(telescope.status, telescope.statlen, "intermediate");
        }else if(strncmp(line, TXT_ANS_COOLERSTAT, strlen(TXT_ANS_COOLERSTAT)) == 0){
            DBG("Got cooler status");
            int s;
            if(sscanf(line + strlen(TXT_ANS_COOLERSTAT), "%d", &s) == 1){
                telescope.cooler = s;
            }
        }else if(strncmp(line, TXT_ANS_COOLERT, strlen(TXT_ANS_COOLERT)) == 0){
            DBG("Got weather ans");
            float m, a;
            if(sscanf(line + strlen(TXT_ANS_COOLERT), "%f,%f", &m, &a) == 2){
                telescope.ambienttemp = a;
                telescope.mirrortemp = m;
            }
        }else if(strncmp(line, TXT_ANS_FOCPOS, strlen(TXT_ANS_FOCPOS)) == 0){
            DBG("Got focuser position");
            int p;
            if(sscanf(line + strlen(TXT_ANS_FOCPOS), "%d", &p) == 1){
                telescope.focuserpos = p;
            }
        }else{
            DBG("Unknown answer: %s", line);
        }
    }
    pthread_mutex_unlock(&telescope.mutex);
}

// dome polling; @return TRUE if all OK
static int poll_device(){
    static const char *reqcmds[] = {TXT_FOCGET, TXT_STATUS, TXT_COOLERT, TXT_COOLERSTAT, NULL};
    for(const char **cmd = reqcmds; *cmd; ++cmd){
        if(write_cmd(*cmd)){
            LOGWARN("Can't write command %s", *cmd);
            DBG("Can't write command %s", *cmd);
            return FALSE;
        }
        serial_parser();
    }
    return TRUE;
}

void runserver(int isunix, const char *node, int maxclients){
    int forbidden = 0;
    if(locksock) sl_sock_delete(&locksock);
    if(rb) sl_RB_delete(&rb);
    rb = sl_RB_new(BUFSIZ);
    telescope.cmd = CMD_NONE;
    FREE(telescope.status);
    telescope.statlen = STATBUF_SZ;
    telescope.status = MALLOC(char, STATBUF_SZ);
    pthread_mutex_init(&telescope.mutex, NULL);
    sl_socktype_e type = (isunix) ? SOCKT_UNIX : SOCKT_NET;
    locksock = sl_sock_run_server(type, node, -1, handlers);
    sl_sock_changemaxclients(locksock, maxclients);
    sl_sock_maxclhandler(locksock, toomuch);
    sl_sock_connhandler(locksock, connected);
    sl_sock_dischandler(locksock, disconnected);
    sl_sock_defmsghandler(locksock, defhandler);
    double tgot = 0.;
    while(locksock && locksock->connected){
        if(ForbidObservations){
            if(!forbidden){
                if(0 == write_cmd(TXT_CLOSE)) forbidden = 1;
                pthread_mutex_lock(&telescope.mutex);
                telescope.cmd = CMD_NONE;
                pthread_mutex_unlock(&telescope.mutex);
            }
        }else forbidden = 0;
        usleep(1000);
        if(!locksock->rthread){
            WARNX("Server handlers thread is dead");
            LOGERR("Server handlers thread is dead");
            break;
        }
        double tnow = sl_dtime();
        if(tnow - tgot > T_INTERVAL){
            if(poll_device()) tgot = tnow;
        }
        pthread_mutex_lock(&telescope.mutex);
        if(telescope.cmd != CMD_NONE){
            switch(telescope.cmd){
            case CMD_OPEN:
                DBG("received command: open");
                if(0 == write_cmd(TXT_OPEN)) telescope.cmd = CMD_NONE;
                break;
            case CMD_CLOSE:
                DBG("received command: close");
                if(0 == write_cmd(TXT_CLOSE)) telescope.cmd = CMD_NONE;
                break;
            case CMD_FOCSTOP:
                DBG("received command: stop focus");
                if(0 == write_cmd(TXT_FOCSTOP)) telescope.cmd = CMD_NONE;
                break;
            case CMD_COOLEROFF:
                DBG("received command: cooler off");
                if(0 == write_cmd(TXT_COOLEROFF)) telescope.cmd = CMD_NONE;
                break;
            case CMD_COOLERON:
                DBG("received command: cooler on");
                if(0 == write_cmd(TXT_COOLERON)) telescope.cmd = CMD_NONE;
                break;
            default:
                DBG("WTF?");
            }
        }
        pthread_mutex_unlock(&telescope.mutex);
        // check abs/rel move
        char buf[256];
        double dt = tnow - Absmove.timestamp;
        if(dt < 3. * T_INTERVAL){
            if(Absmove.val < FOC_MINPOS || Absmove.val > FOC_MAXPOS) Absmove.timestamp = 0.; // reset wrong data
            else{
                snprintf(buf, 255, "%s%d\r", TXT_FOCUSABS, (int)Absmove.val);
                if(0 == write_cmd(buf)){
                    DBG("STARTED absmove");
                    Absmove.timestamp = 0.;
                }
            }
        }
        dt = tnow - Relmove.timestamp;
        if(dt < 3. * T_INTERVAL){
            if(Relmove.val < -FOC_MAXPOS || Relmove.val > FOC_MAXPOS) Relmove.timestamp = 0.;
            else{ // "in" < 0, "out" > 0
                char *cmd = TXT_FOCUSOUT;
                int pos = (int)Relmove.val;
                DBG("pos=%d", pos);
                if(pos < 0){ cmd = TXT_FOCUSIN; pos = -pos; }
                snprintf(buf, 255, "%s%d\r", cmd, pos);
                if(0 == write_cmd(buf)){
                    DBG("STARTED relmove");
                    Relmove.timestamp = 0.;
                }
            }
        }
        serial_parser();
    }
    stopserver();
}

void forbid_observations(int forbid){
    if(forbid){
        ForbidObservations = true;
        LOGWARN("Got forbidden signal");
    }else{
        ForbidObservations = false;
        LOGWARN("Got allowed signal");
    }
    DBG("Change ForbidObservations=%d", forbid);
}
