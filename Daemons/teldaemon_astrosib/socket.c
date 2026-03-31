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

#include "commands.h"
#include "header.h"
#include "socket.h"
#include "term.h"

typedef enum{
    CMD_OPEN,
    CMD_CLOSE,
    CMD_FOCSTOP,
    CMD_COOLERON,
    CMD_COOLEROFF,
    CMD_HEATERON,
    CMD_HEATEROFF,
    CMD_NONE
} tel_commands_t;

typedef struct{
    int focuserpos;     // focuser position
    char status[STATBUF_SZ]; // device status
    double stattime;    // time of last status
    int cooler;         // cooler's status
    int heater;         // heater's status
    double mirrortemp;  // T mirror, degC
    double ambienttemp; // T ambient, degC
    tel_commands_t cmd; // deferred command
    tel_commands_t errcmd; // command ended with error
    pthread_mutex_t mutex;
} tel_t;

static tel_t telescope = {0};

// external signal "deny/allow"
static atomic_bool ForbidObservations = 0; // ==1 if all is forbidden -> close dome and not allow to open

static sl_sock_t *locksock = NULL; // local server socket

// args of absolute/relative focus move commands
static sl_sock_int_t Absmove = {0};
static sl_sock_int_t Relmove = {0};

void stopserver(){
    if(locksock) sl_sock_delete(&locksock);
}

/**
 * @brief get_telescope_data - copy local `telescope` to `t`
 * @param t (i) - pointer to your struct
 * @return true if observations are permitted and false if forbidden
 */
bool get_telescope_data(telstatus_t *t){
    if(!t) return false;
    pthread_mutex_lock(&telescope.mutex);
    *t = *((telstatus_t*)&telescope);
    pthread_mutex_unlock(&telescope.mutex);
    return ForbidObservations;
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
    double t = NAN, mirt, ambt;
    tel_commands_t lastecmd;
    pthread_mutex_lock(&telescope.mutex);
    if(!*telescope.status || sl_dtime() - telescope.stattime > 3.*T_INTERVAL) snprintf(buf, 255, "%s=unknown\n", item->key);
    else{
        snprintf(buf, 255, "%s=%s\n", item->key, telescope.status);
        t = telescope.stattime;
    }
    mirt = telescope.mirrortemp;
    ambt = telescope.ambienttemp;
    lastecmd = telescope.errcmd;
    pthread_mutex_unlock(&telescope.mutex);
    sl_sock_sendstrmessage(client, buf);
    if(!isnan(t)) sendtmeasured(client, t);
    snprintf(buf, 255, "mirrortemp=%.1f\n", mirt);
    sl_sock_sendstrmessage(client, buf);
    snprintf(buf, 255, "ambienttemp=%.1f\n", ambt);
    sl_sock_sendstrmessage(client, buf);
    if(lastecmd != CMD_NONE){
        const char *tcmd;
        switch(lastecmd){
            case CMD_OPEN: tcmd = "open"; break;
            case CMD_CLOSE: tcmd = "close"; break;
            case CMD_FOCSTOP: tcmd = "focstop"; break;
            case CMD_COOLERON: case CMD_COOLEROFF: tcmd = "cooler"; break;
            default: tcmd = "unknown";
        }
        snprintf(buf, 255, "errored_command=%s\n", tcmd);
        sl_sock_sendstrmessage(client, buf);

    }
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

static sl_sock_hresult_e heaterh(sl_sock_t *client, sl_sock_hitem_t *item, const char *req){
    char buf[256];
    if(req){ // getter
        int onoff;
        if(!sl_str2i(&onoff, req)) return RESULT_BADVAL;
        pthread_mutex_lock(&telescope.mutex);
        if(onoff) telescope.cmd = CMD_HEATERON;
        else telescope.cmd = CMD_HEATEROFF;
        pthread_mutex_unlock(&telescope.mutex);
        return RESULT_OK;
    }
    // getter
    pthread_mutex_lock(&telescope.mutex);
    snprintf(buf, 255, "%s=%d\n", item->key, telescope.heater);
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
    {sl_sock_inthandler, "focrel", "relative focus move", (void*)&Relmove},
    {sl_sock_inthandler, "focabs", "absolute focus move", (void*)&Absmove},
    {foch, "focpos", "current focuser position", NULL},
    {openh, "open", "open shutters", NULL},
    {closeh, "close", "close shutters", NULL},
    {statush, "status", "get shutters' status and temperatures", NULL},
    {fstoph, "focstop", "stop focuser moving", NULL},
    {coolerh, "cooler", "get/set cooler status", NULL},
    {heaterh, "heater", "get/set heater status", NULL},
    {dtimeh, "dtime", "get server's UNIX time for all clients connected", NULL},
    {NULL, NULL, NULL, NULL}
};

// dome polling; @return TRUE if all OK
static int poll_device(){
    char ans[ANSLEN];
    char *data;
    int I;

    if(!(data = term_cmdwans(TXT_FOCGET, TXT_ANS_FOCPOS, ans))) return FALSE;
    DBG("\nGot focuser position");
    if(sscanf(data, "%d", &I) == 1){
        pthread_mutex_lock(&telescope.mutex);
        telescope.focuserpos = I;
        pthread_mutex_unlock(&telescope.mutex);
    }

    if(!(data = term_cmdwans(TXT_STATUS, TXT_ANS_STATUS, ans))) return FALSE;
    DBG("\nGot status");
    pthread_mutex_lock(&telescope.mutex);
    telescope.stattime = sl_dtime();
    if(strncmp(data, "0,0,0,0,0", 9) == 0) snprintf(telescope.status, STATBUF_SZ-1, "closed");
    else if(strncmp(data, "1,1,1,1,1", 9) == 0) snprintf(telescope.status, STATBUF_SZ-1, "opened");
    else snprintf(telescope.status, STATBUF_SZ-1, "intermediate");
    pthread_mutex_unlock(&telescope.mutex);

    if(!(data = term_cmdwans(TXT_COOLERT, TXT_ANS_COOLERT, ans))) return FALSE;
    DBG("\nGot weather ans");
    float m, a;
    if(sscanf(data, "%f,%f", &m, &a) == 2){
        pthread_mutex_lock(&telescope.mutex);
        telescope.ambienttemp = a;
        telescope.mirrortemp = m;
        pthread_mutex_unlock(&telescope.mutex);
    }
    if(!(data = term_cmdwans(TXT_COOLERSTAT, TXT_ANS_COOLERSTAT, ans))) return FALSE;
    DBG("\nGot cooler status");
    if(sscanf(data, "%d", &I) == 1){
        pthread_mutex_lock(&telescope.mutex);
        telescope.cooler = I;
        pthread_mutex_unlock(&telescope.mutex);
    }
    if(!(data = term_cmdwans(TXT_HEATSTAT, TXT_ANS_HEATSTAT, ans))) return FALSE;
    DBG("\nGot heater status");
    if(sscanf(data, "%d", &I) == 1){
        pthread_mutex_lock(&telescope.mutex);
        telescope.heater = I;
        pthread_mutex_unlock(&telescope.mutex);
    }
    return TRUE;
}

void runserver(int isunix, const char *node, int maxclients){
    char ans[ANSLEN];
    int forbidden = 0;
    if(locksock) sl_sock_delete(&locksock);
    telescope.errcmd = telescope.cmd = CMD_NONE;
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
                if(term_cmdwans(TXT_CLOSE, TXT_ANS_OK, ans)) forbidden = 1;
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
            if(poll_device()){
                tgot = tnow;
                write_header();
            }
        }
        if(ForbidObservations) continue;
        pthread_mutex_lock(&telescope.mutex);
        tel_commands_t tcmd = telescope.cmd;
        pthread_mutex_unlock(&telescope.mutex);
        if(tcmd != CMD_NONE){
            switch(tcmd){
            case CMD_OPEN:
                DBG("received command: open");
                if(term_cmdwans(TXT_OPEN, TXT_ANS_OK, ans)){
                    LOGMSG("Open dome");
                    tcmd = CMD_NONE;
                }
                break;
            case CMD_CLOSE:
                DBG("received command: close");
                if(term_cmdwans(TXT_CLOSE, TXT_ANS_OK, ans)){
                    LOGMSG("Close dome");
                    tcmd = CMD_NONE;
                }
                break;
            case CMD_FOCSTOP:
                DBG("received command: stop focus");
                LOGMSG("Stop focus");
                term_write(TXT_FOCSTOP, ans); tcmd = CMD_NONE; // erroreous thing: no answer for this command
                break;
            case CMD_COOLEROFF:
                DBG("received command: cooler off");
                if(term_cmdwans(TXT_COOLEROFF, TXT_ANS_OK, ans)){
                    LOGMSG("Cooler off");
                    tcmd = CMD_NONE;
                }
                break;
            case CMD_COOLERON:
                DBG("received command: cooler on");
                if(term_cmdwans(TXT_COOLERON, TXT_ANS_OK, ans)){
                    LOGMSG("Cooler on");
                    tcmd = CMD_NONE;
                }
                break;
            case CMD_HEATERON:
                DBG("received command: heater on");
                if(term_cmdwans(TXT_HEATON, TXT_ANS_OK, ans)){
                    LOGMSG("Heater on");
                    tcmd = CMD_NONE;
                }
                break;
            case CMD_HEATEROFF:
                DBG("received command: heater off");
                if(term_cmdwans(TXT_HEATOFF, TXT_ANS_OK, ans)){
                    LOGMSG("Heater off");
                    tcmd = CMD_NONE;
                }
                break;
            default:
                DBG("WTF?");
            }
        }
        pthread_mutex_lock(&telescope.mutex);
        telescope.cmd = telescope.errcmd = tcmd;
        pthread_mutex_unlock(&telescope.mutex);
        // check abs/rel move
        char buf[256];
        double dt = tnow - Absmove.timestamp;
        if(dt < 3. * T_INTERVAL){
            if(Absmove.val < FOC_MINPOS || Absmove.val > FOC_MAXPOS) Absmove.timestamp = 0.; // reset wrong data
            else{
                snprintf(buf, 255, "%s%d\r", TXT_FOCUSABS, (int)Absmove.val);
                if(term_cmdwans(buf, TXT_ANS_OK, ans)){
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
                if(term_cmdwans(buf, TXT_ANS_OK, ans)){
                    DBG("STARTED relmove");
                    Relmove.timestamp = 0.;
                }
            }
        }
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
