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

#include <signal.h>
#include <string.h>
#include <usefull_macros.h>

#include "sensors.h"
#include "server.h"

// if measurement time oldest than now minus `oldest_interval`, we think measurement are too old
static time_t oldest_interval = 60;

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
    sensordata_t d;
    for(int i = 0; i < N; ++i){
        if(!get_plugin(&d, i)) continue;
        snprintf(buf, 255, "PLUGIN[%d]=%s\nNVALUES[%d]=%d\n", i, d.name, i, d.Nvalues);
        sl_sock_sendstrmessage(client, buf);
    }
    return RESULT_SILENCE;
}

/**
 * @brief showdata - send to user meteodata
 * @param client - client data
 * @param N - index of station
 * @param showidx - == TRUE to show index in square brackets
 */
static void showdata(sl_sock_t *client, int N, int showidx){
    char buf[FULL_LEN+1];
    val_t v;
    sensordata_t s;
    if(!get_plugin(&s, N) || (s.Nvalues < 1)){
        snprintf(buf, FULL_LEN, "Can't get plugin[%d]\n", N);
        sl_sock_sendstrmessage(client, buf);
        return;
    }
    if(!showidx || get_nplugins() == 1) N = -1; // only one -> don't show indexes
    time_t oldest = time(NULL) - oldest_interval;
    uint64_t Tsum = 0; int nsum = 0;
    for(int i = 0; i < s.Nvalues; ++i){
        if(!s.get_value(&v, i)) continue;
        if(v.time < oldest) continue;
        if(1 > format_sensval(&v, buf, FULL_LEN+1, N)) continue;
        DBG("formatted: '%s'", buf);
        sl_sock_sendstrmessage(client, buf);
        sl_sock_sendbyte(client, '\n');
        ++nsum; Tsum += v.time;
    }
    oldest = (time_t)(Tsum / nsum);
    if(0 < format_msrmttm(oldest, buf, FULL_LEN+1)){ // send mean measuring time
        DBG("Formatted time: '%s'", buf);
        sl_sock_sendstrmessage(client, buf);
        sl_sock_sendbyte(client, '\n');
    }
}

// get meteo data
static sl_sock_hresult_e gethandler(sl_sock_t *client, _U_ sl_sock_hitem_t *item, const char *req){
    if(!client) return RESULT_FAIL;
    int N = get_nplugins();
    if(N < 1) return RESULT_FAIL;
    if(!req) for(int i = 0; i < N; ++i) showdata(client, i, TRUE);
    else{
        int n;
        if(!sl_str2i(&n, req) || n < 0 || n >= N) return RESULT_BADVAL;
        showdata(client, n, FALSE);
    }
    return RESULT_SILENCE;
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

// handlers for network and local (UNIX) sockets
static sl_sock_hitem_t nethandlers[] = { // net - only getters and client-only setters
    {gethandler,  "get",  "get all meteo or only for given plugin number", NULL},
    {listhandler, "list", "show all opened plugins", NULL},
    {timehandler, "time", "get server's UNIX time", NULL},
    {NULL, NULL, NULL, NULL}
};
static sl_sock_hitem_t localhandlers[] = { // local - full amount of setters/getters
    {gethandler,  "get",  "get all meteo or only for given plugin number", NULL},
    {listhandler, "list", "show all opened plugins", NULL},
    {timehandler, "time", "get server's UNIX time", NULL},
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
    //pthread_kill(locthread, 9);
    //pthread_kill(netthread, 9);
    //LOGMSG("Server threads killed");
}
