/*
 * This file is part of the weatherdaemon project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <ctype.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <usefull_macros.h>

#include "mainweather.h"
#include "sensors.h"
#include "server.h"

// server's sockets: net and local (UNIX)
static sl_sock_t *netsocket = NULL, *localsocket;
//static pthread_t netthread, locthread;

// show user current time
static sl_sock_hresult_e timehandler(sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(!client) return RESULT_FAIL;
    char buf[32];
    snprintf(buf, 31, "UNIXT=%.3f\n", sl_dtime());
    sl_sock_sendstrmessage(client, buf);
    return RESULT_SILENCE;
}

// show all connected libraries
static sl_sock_hresult_e listhandler(sl_sock_t *client, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    if(!client) return RESULT_FAIL;
    char buf[256];
    int N = get_nplugins();
    if(N < 1) return RESULT_FAIL;
    sensordata_t *d = NULL;
    for(int i = 0; i < N; ++i){
        if(!(d = get_plugin(i))) continue;
        snprintf(buf, 255, "PLUGIN[%d]=%s\nNVALUES[%d]=%d\n", i, d->name, i, d->Nvalues);
        sl_sock_sendstrmessage(client, buf);
    }
    return RESULT_SILENCE;
}

// get N'th plugin or send error message
sensordata_t *get_plugin_w_message(sl_sock_t *client, int N){
    char buf[FULL_LEN];
    sensordata_t *s = NULL;
    if(!(s = get_plugin(N)) || (s->Nvalues < 1)){
        snprintf(buf, FULL_LEN, "Can't get plugin[%d]\n", N);
        sl_sock_sendstrmessage(client, buf);
        return NULL;
    }
    return s;
}

/**
 * @brief showdata - send to client sensor's data
 * @param client  - client data
 * @param N - -1 for common data or station index for specific meteo
 */
static void showdata(sl_sock_t *client, int N){
    char buf[FULL_LEN];
    val_t v;
    int Ncoll = 0;
    sensordata_t *s = NULL;
    if(N < 0){
        Ncoll = collected_amount();
    }else{
        s = get_plugin_w_message(client, N);
        if(!s) return;
        Ncoll = s->Nvalues;
    }
    time_t oldest = time(NULL) - WeatherConf.ahtung_delay, mstm = 0;

    for(int i = 0; i < Ncoll; ++i){
        int ans = 0;
        if(N < 0) ans = get_collected(&v, i);
        else ans = s->get_value(s, &v, i);
        if(!ans){ DBG("Can't get %dth value", i); continue; }
        // hide old and broken sensors data
        if(v.time < oldest || v.sense > VAL_UNNECESSARY){ /*DBG("%dth value is too old", i);*/ continue; }
        if(1 > format_sensval(&v, buf, FULL_LEN, N)){ DBG("Can't format %d", i); continue; }
        //DBG("formatted: '%s'", buf);
        sl_sock_sendstrmessage(client, buf);
        sl_sock_sendbyte(client, '\n');
        if(v.time > mstm) mstm = v.time;
    }
    // also send FORCE flag if have
    if(N < 0 && is_forbidden()) sl_sock_sendstrmessage(client, "FORBID  =                    1 / Observations are forbidden by operator\n");
    if(mstm){
        if(0 < format_msrmttm(mstm, buf, FULL_LEN, N)){ // send mean measuring time
            //DBG("Formatted time: '%s'", buf);
            sl_sock_sendstrmessage(client, buf);
            sl_sock_sendbyte(client, '\n');
        }
    }
}


// get meteo data
static sl_sock_hresult_e gethandler(sl_sock_t *client, _U_ sl_sock_hitem_t *item, const char *req){
    if(!client) return RESULT_FAIL;
    int N = get_nplugins();
    if(N < 1) return RESULT_FAIL;
    if(!req) showdata(client, -1);
    else{
        int n;
        if(!sl_str2i(&n, req) || n < 0 || n >= N) return RESULT_BADVAL;
        showdata(client, n);
    }
    return RESULT_SILENCE;
}

// get parameters' level
static sl_sock_hresult_e getlvlhandler(sl_sock_t *client, _U_ sl_sock_hitem_t *item, const char *req){
    if(!client) if(!client) return RESULT_FAIL;
    int N = get_nplugins();
    if(N < 1) return RESULT_FAIL;
    val_t v;
    char buf[FULL_LEN];
    if(!req){ // level of collected parameters
        DBG("User asks for collected");
        int Ncoll = collected_amount();
        if(Ncoll < 1) return RESULT_FAIL;
        for(int i = 0; i < Ncoll; ++i){
            if(!get_collected(&v, i)){ DBG("Can't get %dth value", i); continue; }
            if(1 > format_senssense(&v, buf, FULL_LEN, -1)){ DBG("Can't format"); continue; }
            sl_sock_sendstrmessage(client, buf);
            sl_sock_sendbyte(client, '\n');
        }
    }else{
        int n;
        if(!sl_str2i(&n, req) || n < 0 || n >= N) return RESULT_BADVAL;
        DBG("User asks for %d", n);
        sensordata_t *s = get_plugin_w_message(client, n);
        if(!s) return RESULT_SILENCE;
        for(int i = 0; i < s->Nvalues; ++i){
            if(!s->get_value(s, &v, i)) continue;
            if(1 > format_senssense(&v, buf, FULL_LEN, n)){ DBG("Can't format"); continue; }
            sl_sock_sendstrmessage(client, buf);
            sl_sock_sendbyte(client, '\n');
        }
    }
    return RESULT_SILENCE;
}

// set parameters' level; format: setlevel=N1:par=level,...,N1:par=level...
// Nx - "station" number, par - parameter name (like "HUMIDITY"), level - new level (0..3)
static sl_sock_hresult_e setlvlhandler(sl_sock_t *client, _U_ sl_sock_hitem_t *item, const char *req){
    if(!client) if(!client) return RESULT_FAIL;
    int N = get_nplugins();
    if(N < 1) return RESULT_FAIL;
    if(!req) return RESULT_BADVAL;
    sl_sock_hresult_e result = RESULT_OK;
    char *s = (char *)req;
    while(*s){
        while (isspace(*s) || *s == ',') s++;
        if (!*s) break;
        // get station number
        char *end;
        long st_num = strtol(s, &end, 10);
        if(s == end) break;
        s = end;
        // wait for ':'
        while (isspace(*s)) s++;
        if(*s != ':') break;
        ++s;
        DBG("ST %ld:\n", st_num);
        sensordata_t *sd;
        if(st_num < 0 || st_num >= N || !(sd = get_plugin((int)st_num))){
            result = RESULT_BADVAL;
            break;
        }
        while(1){
            while(isspace(*s)) ++s;
            if(*s == '\0') break;
            if(isdigit((unsigned char)*s)) break; // - next block
            const char *par_start = s;
            while(isalnum(*s)) ++s;
            if(par_start == s) break;
            int l = (int)(s - par_start);
            DBG("  par: %.*s = ", l, par_start);
            if(l > KEY_LEN){
                result = RESULT_BADVAL;
                break;
            }
            char buf[KEY_LEN + 1];
            memcpy(buf, par_start, l);
            buf[l] = 0;
            int validx = find_val_by_name(sd, buf);
            // search for given value
            if(validx < 0){
                result = RESULT_BADVAL;
                break;
            }
            while(isspace(*s)) ++s;
            // fait for '='
            if(*s != '='){
                result = RESULT_BADVAL;
                break;
            }
            ++s;
            // get new level
            while(isspace(*s)) s++;
            long val = strtol(s, &end, 10);
            if(s == end){
                result = RESULT_BADVAL;
                break;
            }
            s = end;
            DBG("%ld\n", val);
            if(!change_val_sense(sd, validx, (valsense_t)val)){
                result = RESULT_BADVAL;
                break;
            }
            while(isspace((unsigned char)*s)) s++;
            if(*s == ','){ // omit delimeter
                ++s;
                continue;
            }else{
                if(*s == ';') ++s; // omit ; as block's end
                break;
            }
        }
    }
    return result;
}

// graceful closing socket: let client know that he's told to fuck off
static void toomuch(int fd){
    const char *m = "Try later: too much clients connected\n";
    send(fd, m, sizeof(m)-1, MSG_NOSIGNAL);
    shutdown(fd, SHUT_WR);
    DBG("shutdown, wait");
    double t0 = sl_dtime();
    uint8_t buf[8];
    while(sl_dtime() - t0 < 90.){ // change this value to smaller for real work
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
    if(c->type == SOCKT_UNIX) LOGMSG("New local client fd=%d connected", c->fd);
    else LOGMSG("New client fd=%d, IP=%s connected", c->fd, c->IP);
    return TRUE;
}
// disconnected handler
static void disconnected(sl_sock_t *c){
    if(c->type == SOCKT_UNIX) LOGMSG("Disconnected local client fd=%d", c->fd);
    else LOGMSG("Disconnected client fd=%d, IP=%s", c->fd, c->IP);
}
static sl_sock_hresult_e defhandler(struct sl_sock *s, const char *str){
    if(!s || !str) return RESULT_FAIL;
    sl_sock_sendstrmessage(s, "You entered wrong command:\n```\n");
    sl_sock_sendstrmessage(s, str);
    sl_sock_sendstrmessage(s, "\n```\nTry \"help\"\n");
    return RESULT_SILENCE;
}

#define COMMONHANDLERS \
    {gethandler,  "get",  "get all meteo or only for given plugin number", NULL}, \
    {getlvlhandler,"chklevel",  "check 'sense level' of given plugin parameters", NULL}, \
    {listhandler, "list", "show all opened plugins", NULL}, \
    {timehandler, "time", "get server's UNIX time", NULL},

// handlers for network and local (UNIX) sockets
static sl_sock_hitem_t nethandlers[] = { // net - only getters and client-only setters
    COMMONHANDLERS
    {NULL, NULL, NULL, NULL}
};
static sl_sock_hitem_t localhandlers[] = { // local - full amount of setters/getters
    {setlvlhandler, "setlevel", "set 'sense level' (0..3) for given plugin parameters, e.g. setlevel=1:WIND=3,HUMIDITY=3 - disable fields for station 1", NULL},
    COMMONHANDLERS
    {NULL, NULL, NULL, NULL}
};

#if 0
// common parsers for both net and local sockets
static void *cmdparser(void *U){
    if(!U) return NULL;
    sl_sock_t *s = (sl_sock_t*) U;
    while(s && s->connected){
        if(!s->rthread){
            LOGERR("Server's handlers' thread is dead");
            break;
        }
    }
    LOGDBG("cmdparser(): exit");
    return NULL;
}
#endif

int start_servers(const char *netnode, const char *sockpath){
    if(!netnode || !sockpath){
        LOGERR("start_servers(): need arguments");
        return FALSE;
    }
    netsocket = sl_sock_run_server(SOCKT_NET, netnode, BUFSIZ, nethandlers);
    if(!netsocket){
        LOGERR("start_servers(): can't run network socket");
        return FALSE;
    }
    localsocket = sl_sock_run_server(SOCKT_UNIX, sockpath, BUFSIZ, localhandlers);
    if(!localsocket){
        LOGERR("start_servers(): can't run local socket");
        return FALSE;
    }
    sl_sock_changemaxclients(netsocket, MAX_CLIENTS);
    sl_sock_changemaxclients(localsocket, 1);
    sl_sock_maxclhandler(netsocket, toomuch);
    sl_sock_maxclhandler(localsocket, toomuch);
    sl_sock_connhandler(netsocket, connected);
    sl_sock_connhandler(localsocket, connected);
    sl_sock_dischandler(netsocket, disconnected);
    sl_sock_dischandler(localsocket, disconnected);
    sl_sock_defmsghandler(netsocket, defhandler);
    sl_sock_defmsghandler(localsocket, defhandler);
#if 0
    if(pthread_create(&netthread, NULL, cmdparser, (void*)netsocket)){
        LOGERR("Can't run server's net thread");
        goto errs;
    }
    if(pthread_create(&locthread, NULL, cmdparser, (void*)localsocket)){
        LOGERR("Can't run server's local thread");
        goto errs;
    }
#endif
    return TRUE;
#if 0
errs:
    sl_sock_delete(&localsocket);
    sl_sock_delete(&netsocket);
    return FALSE;
#endif
}

void kill_servers(){
    //pthread_cancel(locthread);
    //pthread_cancel(netthread);
    //LOGMSG("Server threads canceled");
    //usleep(500);
    sl_sock_delete(&localsocket);
    sl_sock_delete(&netsocket);
    LOGMSG("Server sockets destroyed");
    //usleep(500);
    //pthread_join(locthread, NULL);
    //pthread_join(netthread, NULL);
    //LOGMSG("Server threads are dead");
}
