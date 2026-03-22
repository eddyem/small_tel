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

#include <inttypes.h>
#include <pthread.h>
#include <string.h>

#include "socket.h"
#include "term.h"

typedef enum{
    CMD_OPEN,
    CMD_CLOSE,
    CMD_STOP,
    CMD_NONE
} dome_commands_t;

typedef struct{
    dome_commands_t cmd;
    int errcode;    // error code
    char *status;   // device status
    int statlen;    // size of `status` buffer
    double stattime;// time of last status
    char *weather;  // data from weather sensor
    int weathlen;   // length of `weather` buffer
    double weathtime;// time of last weather
    pthread_mutex_t mutex;
} dome_t;

static dome_t Dome = {0};

static sl_sock_t *locksock = NULL;
static sl_ringbuffer_t *rb = NULL; // incoming serial data

void stopserver(){
    if(locksock) sl_sock_delete(&locksock);
    if(rb) sl_RB_delete(&rb);
}

#if 0
// flags for standard handlers
static sl_sock_int_t iflag = {0};
static sl_sock_double_t dflag = {0};
static sl_sock_string_t sflag = {0};
static uint32_t bitflags = 0;

static sl_sock_hresult_e show(sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(locksock && locksock->type != SOCKT_UNIX){
        if(*client->IP){
            printf("Client \"%s\" (fd=%d) ask for flags:\n", client->IP, client->fd);
        }else printf("Can't get client's IP, flags:\n");
    }else printf("Socket fd=%d asks for flags:\n", client->fd);
    printf("\tiflag=%" PRId64 ", dflag=%g\n", iflag.val, dflag.val);
    return RESULT_OK;
}
#endif

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

static sl_sock_hresult_e openh(_U_ sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    pthread_mutex_lock(&Dome.mutex);
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
    Dome.cmd = CMD_STOP;
    pthread_mutex_unlock(&Dome.mutex);
    return RESULT_OK;
}

static sl_sock_hresult_e statush(sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[256];
    double t = NAN;
    int ecode;
    pthread_mutex_lock(&Dome.mutex);
    if(!*Dome.status || sl_dtime() - Dome.stattime > 3.*T_INTERVAL) snprintf(buf, 255, "%s=unknown\n", item->key);
    else{
        snprintf(buf, 255, "%s=%s\n", item->key, Dome.status);
        t = Dome.stattime;
    }
    sl_sock_sendstrmessage(client, buf);
    if(!isnan(t)) sendtmeasured(client, t);
    ecode = Dome.errcode;
    pthread_mutex_unlock(&Dome.mutex);
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
    return RESULT_SILENCE;
}

static sl_sock_hresult_e weathh(sl_sock_t *client, sl_sock_hitem_t *item, _U_ const char *req){
    char buf[256];
    double t = NAN;
    pthread_mutex_lock(&Dome.mutex);
    if(!*Dome.weather || sl_dtime() - Dome.weathtime > 3.*T_INTERVAL) snprintf(buf, 255, "%s=unknown\n", item->key);
    else{
        snprintf(buf, 255, "%s=%s\n", item->key, Dome.weather);
        t = Dome.weathtime;
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
#if 0
// if we use this macro, there's no need to run `sl_sock_keyno_init` later
static sl_sock_keyno_t kph_number = SL_SOCK_KEYNO_DEFAULT;
// handler for key with optional parameter number
static sl_sock_hresult_e keyparhandler(struct sl_sock *s, sl_sock_hitem_t *item, const char *req){
    if(!item->data) return RESULT_FAIL;
    char buf[1024];
    int no = sl_sock_keyno_check((sl_sock_keyno_t*)item->data);
    long long newval = -1;
    if(req){
        if(!sl_str2ll(&newval, req) || newval < 0 || newval > 0xffffffff) return RESULT_BADVAL;
    }
    printf("no = %d\n", no);
    if(no < 0){ // flags as a whole
        if(req) bitflags = (uint32_t)newval;
        snprintf(buf, 1023, "flags = 0x%08X\n", bitflags);
        sl_sock_sendstrmessage(s, buf);
    }else if(no < 32){ // bit access
        int bitmask = 1 << no;
        if(req){
            if(newval) bitflags |= bitmask;
            else bitflags &= ~bitmask;
        }
        snprintf(buf, 1023, "flags[%d] = %d\n", no, bitflags & bitmask ? 1 : 0);
        sl_sock_sendstrmessage(s, buf);
    }else return RESULT_BADKEY;
    return RESULT_SILENCE;
}
#endif

static sl_sock_hitem_t handlers[] = {
#if 0
    {sl_sock_inthandler, "int", "set/get integer flag", (void*)&iflag},
    {sl_sock_dblhandler, "dbl", "set/get double flag", (void*)&dflag},
    {sl_sock_strhandler, "str", "set/get string variable", (void*)&sflag},
    {keyparhandler, "flags", "set/get bit flags as whole (flags=val) or by bits (flags[bit]=val)", (void*)&kph_number},
    {show, "show", "show current flags @ server console", NULL},
#endif
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
    char line[256];
    if(write_cmd(TXT_GETWARN)){
        DBG("Can't write command warning");
        return FALSE;
    }
    if(write_cmd(TXT_GETSTAT)){
        DBG("Can't write command getstat");
        return FALSE;
    }
    if(write_cmd(TXT_GETWEAT)){
        DBG("Can't write command getweath");
        return FALSE;
    }
    int l = 0;
    do{
        l = read_term(line, 256);
        if(l > 0) sl_RB_write(rb, (uint8_t*) line, l);
    }while(l > 0);
    pthread_mutex_lock(&Dome.mutex); // prepare user buffers
    // read ringbuffer, run parser and change buffers in `Dome`
    while(sl_RB_readline(rb, line, sizeof(line)) > 0){
        if(strncmp(line, TXT_ANS_STAT, strlen(TXT_ANS_STAT)) == 0){
            DBG("Got status ans");
            int stat;
            Dome.stattime = sl_dtime();
            if(sscanf(line + strlen(TXT_ANS_STAT), "%d", &stat) == 1){
                if(stat == 1111)
                    snprintf(Dome.status, Dome.statlen, "opened");
                else if(stat == 2222)
                    snprintf(Dome.status, Dome.statlen, "closed");
                else
                    snprintf(Dome.status, Dome.statlen, "intermediate");
            }
        }else if(strncmp(line, TXT_ANS_ERR, strlen(TXT_ANS_ERR)) == 0){
            DBG("Got status errno");
            int ecode;
            if(sscanf(line + strlen(TXT_ANS_ERR), "%d", &ecode) == 1){
                Dome.errcode = ecode;
                DBG("errcode: %d", ecode);
            }
        }else if(strncmp(line, TXT_ANS_WEAT, strlen(TXT_ANS_WEAT)) == 0){
            DBG("Got weather ans");
            int weather;
            Dome.weathtime = sl_dtime();
            if(sscanf(line + strlen(TXT_ANS_WEAT), "%d", &weather) == 1){
                if(weather == 0)
                    snprintf(Dome.weather, Dome.weathlen, "good weather");
                else if (weather == 1)
                    snprintf(Dome.weather, Dome.weathlen, "rain or clouds");
                else
                    snprintf(Dome.weather, Dome.weathlen, "unknown");
            }
        }else{
            DBG("Unknown answer: %s", line);
        }
    }
    pthread_mutex_unlock(&Dome.mutex);
    return TRUE;
}

void runserver(int isunix, const char *node, int maxclients){
    if(locksock) sl_sock_delete(&locksock);
    if(rb) sl_RB_delete(&rb);
    rb = sl_RB_new(BUFSIZ);
    Dome.cmd = CMD_NONE;
    FREE(Dome.status);
    Dome.statlen = STATBUF_SZ;
    Dome.status = MALLOC(char, STATBUF_SZ);
    FREE(Dome.weather);
    Dome.weathlen = STATBUF_SZ;
    Dome.weather = MALLOC(char, STATBUF_SZ);
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
        usleep(1000);
        if(!locksock->rthread){
            WARNX("Server handlers thread is dead");
            LOGERR("Server handlers thread is dead");
            break;
        }
        if(sl_dtime() - tgot > T_INTERVAL){
            if(poll_device()) tgot = sl_dtime();
        }
        pthread_mutex_lock(&Dome.mutex);
        if(Dome.cmd != CMD_NONE){
            switch(Dome.cmd){
            case CMD_OPEN:
                DBG("received command: open");
                if(0 == write_cmd(TXT_OPENDOME)) Dome.cmd = CMD_NONE;
                break;
            case CMD_CLOSE:
                DBG("received command: close");
                if(0 == write_cmd(TXT_CLOSEDOME)) Dome.cmd = CMD_NONE;
                break;
            case CMD_STOP:
                DBG("received command: stop");
                if(0 == write_cmd(TXT_STOPDOME)) Dome.cmd = CMD_NONE;
                break;
            default:
                DBG("WTF?");
            }
        }
        pthread_mutex_unlock(&Dome.mutex);
    }
    stopserver();
}
