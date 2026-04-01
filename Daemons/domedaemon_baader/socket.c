/**
 * This file is part of the baader_dome project.
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
    CMD_STOP,
    CMD_NONE
} dome_commands_t;

typedef struct{
    int errcode;    // error code
    char status[STATBUF_SZ]; // device status
    double stattime;// time of last status
    char weather[STATBUF_SZ];  // data from weather sensor
    dome_commands_t cmd;
    dome_commands_t erroredcmd; // command didn't run - waiting or stalled
    pthread_mutex_t mutex;
} dome_t;

static dome_t Dome = {0};

// external signal "deny/allow"
static atomic_bool ForbidObservations = 0; // ==1 if all is forbidden -> close dome and not allow to open

static sl_sock_t *locksock = NULL; // local server socket
static sl_ringbuffer_t *rb = NULL; // incoming serial data

int get_dome_data(dome_data_t *d){
    if(!d) return FALSE;
    pthread_mutex_lock(&Dome.mutex);
    *d = *((dome_data_t*)&Dome);
    pthread_mutex_unlock(&Dome.mutex);
    return ForbidObservations;
}

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

#define CHKALLOWED() do{if(ForbidObservations || Dome.errcode){pthread_mutex_unlock(&Dome.mutex); return RESULT_FAIL;}}while(0)

static sl_sock_hresult_e openh(_U_ sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    pthread_mutex_lock(&Dome.mutex);
    CHKALLOWED();
    Dome.cmd = CMD_OPEN;
    pthread_mutex_unlock(&Dome.mutex);
    return RESULT_OK;
}

static sl_sock_hresult_e closeh(_U_ sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    pthread_mutex_lock(&Dome.mutex);
    Dome.cmd = CMD_CLOSE;
    pthread_mutex_unlock(&Dome.mutex);
    return RESULT_OK;
}

static sl_sock_hresult_e stoph(_U_ sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    pthread_mutex_lock(&Dome.mutex);
    CHKALLOWED();
    Dome.cmd = CMD_STOP;
    pthread_mutex_unlock(&Dome.mutex);
    return RESULT_OK;
}

static sl_sock_hresult_e statush(sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[256];
    double t = NAN;
    int ecode, lastecmd;
    pthread_mutex_lock(&Dome.mutex);
    if(!*Dome.status || sl_dtime() - Dome.stattime > 3.*T_INTERVAL) snprintf(buf, 255, "%s=unknown\n", item->key);
    else{
        snprintf(buf, 255, "%s=%s\n", item->key, Dome.status);
        t = Dome.stattime;
    }
    ecode = Dome.errcode;
    lastecmd = Dome.erroredcmd;
    pthread_mutex_unlock(&Dome.mutex);
    sl_sock_sendstrmessage(client, buf);
    if(!isnan(t)) sendtmeasured(client, t);
    if(ecode){
        int l = snprintf(buf, 255, "error=closed");
        if(ecode > 99){
            ecode -= 100;
            int n = snprintf(buf+l, 255-l, "@rain");
            l += n;
        }
        if(ecode > 9){
            ecode -= 10;
            int n = snprintf(buf+l, 255-l, "@timeout");
            l += n;
        }
        if(ecode){
            int n = snprintf(buf+l, 255-l, "@powerloss");
            l += n;
        }
        snprintf(buf+l, 255-l, "\n");
        sl_sock_sendstrmessage(client, buf);
    }
    if(ForbidObservations) sl_sock_sendstrmessage(client, "FORBIDDEN\n");
    if(lastecmd != CMD_NONE){
        const char *tcmd;
        switch(lastecmd){
            case CMD_OPEN: tcmd = "open"; break;
            case CMD_CLOSE: tcmd = "close"; break;
            case CMD_STOP: tcmd = "stop"; break;
            default: tcmd = "unknown";
        }
        snprintf(buf, 255, "errored_command=%s\n", tcmd);
        sl_sock_sendstrmessage(client, buf);
    }
    return RESULT_SILENCE;
}

static sl_sock_hresult_e weathh(sl_sock_t *client, sl_sock_hitem_t *item, _U_ const char *req){
    char buf[256];
    double t = NAN;
    pthread_mutex_lock(&Dome.mutex);
    if(sl_dtime() - Dome.stattime > 3.*T_INTERVAL) snprintf(buf, 255, "%s=unknown\n", item->key);
    else{
        snprintf(buf, 255, "%s=%s\n", item->key, Dome.weather);
        t = Dome.stattime;
    }
    pthread_mutex_unlock(&Dome.mutex);
    sl_sock_sendstrmessage(client, buf);
    if(!isnan(t)) sendtmeasured(client, t);
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
    {openh, "open", "open dome", NULL},
    {closeh, "close", "close dome", NULL},
    {statush, "status", "get dome status", NULL},
    {stoph, "stop", "stop dome operations", NULL},
    {weathh, "weather", "weather sensor status", NULL},
    {dtimeh, "dtime", "get server's UNIX time for all clients connected", NULL},
    {NULL, NULL, NULL, NULL}
};

// dome polling; @return TRUE if all OK
static int poll_device(){
    char ans[ANSLEN];
    char *data;
    if(!(data = term_cmdwans(TXT_GETWARN, TXT_ANS_ERR, ans))) return FALSE;
    DBG("Got status errno");
    int I;
    if(sscanf(data, "%d", &I) == 1){
        pthread_mutex_lock(&Dome.mutex);
        Dome.errcode = I;
        pthread_mutex_unlock(&Dome.mutex);
        DBG("errcode: %d", I);
    }
    if(!(data = term_cmdwans(TXT_GETSTAT, TXT_ANS_STAT, ans))) return FALSE;
    DBG("Got status ans");
    if(sscanf(data, "%d", &I) == 1){
        pthread_mutex_lock(&Dome.mutex);
        if(I == 1111)
            snprintf(Dome.status, STATBUF_SZ-1, "opened");
        else if(I == 2222)
            snprintf(Dome.status, STATBUF_SZ-1, "closed");
        else
            snprintf(Dome.status, STATBUF_SZ-1, "intermediate");
        Dome.stattime = sl_dtime();
        pthread_mutex_unlock(&Dome.mutex);
    }
    if(!(data = term_cmdwans(TXT_GETWEAT, TXT_ANS_WEAT, ans))) return FALSE;
    DBG("Got weather ans");
    if(sscanf(data, "%d", &I) == 1){
        pthread_mutex_lock(&Dome.mutex);
        if(I == 0)
            snprintf(Dome.weather, STATBUF_SZ-1, "good");
        else if (I == 1)
            snprintf(Dome.weather, STATBUF_SZ-1, "rain/clouds");
        else
            snprintf(Dome.weather, STATBUF_SZ-1, "unknown");
        pthread_mutex_unlock(&Dome.mutex);
    }
    return TRUE;
}

void runserver(int isunix, const char *node, int maxclients){
    int forbidden = 0;
    char ans[ANSLEN];
    if(locksock) sl_sock_delete(&locksock);
    if(rb) sl_RB_delete(&rb);
    rb = sl_RB_new(BUFSIZ);
    Dome.cmd = CMD_NONE;
    pthread_mutex_init(&Dome.mutex, NULL);
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
                if(term_cmdwans(TXT_CLOSEDOME, TXT_ANS_MSGOK, ans)) forbidden = 1;
            }
            pthread_mutex_lock(&Dome.mutex);
            if(Dome.cmd != CMD_NONE){
                Dome.erroredcmd = Dome.cmd;
                Dome.cmd = CMD_NONE;
            }
            pthread_mutex_unlock(&Dome.mutex);
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
        pthread_mutex_lock(&Dome.mutex);
        if(Dome.cmd != CMD_NONE){
            switch(Dome.cmd){
            case CMD_OPEN:
                DBG("received command: open");
                if(term_cmdwans(TXT_OPENDOME, TXT_ANS_MSGOK, ans)) Dome.cmd = CMD_NONE;
                break;
            case CMD_CLOSE:
                DBG("received command: close");
                if(term_cmdwans(TXT_CLOSEDOME, TXT_ANS_MSGOK, ans)) Dome.cmd = CMD_NONE;
                break;
            case CMD_STOP:
                DBG("received command: stop");
                if(term_cmdwans(TXT_STOPDOME, TXT_ANS_MSGOK, ans)) Dome.cmd = CMD_NONE;
                break;
            default:
                DBG("WTF?");
            }
        }
        Dome.erroredcmd = Dome.cmd; // if command didn't run last time, it will store here
        pthread_mutex_unlock(&Dome.mutex);
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
