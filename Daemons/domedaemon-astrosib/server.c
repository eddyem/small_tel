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

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <usefull_macros.h>

#include "dome.h"

// max age time of last status - 30s
#define STATUS_MAX_AGE      (30.)

// commands
#define CMD_UNIXT       "unixt"
#define CMD_STATUS      "status"
#define CMD_STATUST     "statust"
#define CMD_RELAY       "relay"
#define CMD_OPEN        "open"
#define CMD_CLOSE       "close"
#define CMD_STOP        "stop"
#define CMD_HALF        "half"

// main socket
static sl_sock_t *s = NULL;

/////// handlers
// unixt - send to ALL clients
static sl_sock_hresult_e dtimeh(sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[32];
    snprintf(buf, 31, "%s=%.2f\n", item->key, sl_dtime());
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}
// status - get current dome status in old format (first four numbers)
static sl_sock_hresult_e statush(sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[128];
    dome_status_t dome_status;
    double lastt = get_dome_status(&dome_status);
    if(sl_dtime() - lastt > STATUS_MAX_AGE) return RESULT_FAIL;
    snprintf(buf, 127, "%s=%d,%d,%d,%d\n", item->key, dome_status.coverstate[0],
            dome_status.coverstate[1], dome_status.encoder[0], dome_status.encoder[1]);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}
static const char *textst(int coverstate){
    switch(coverstate){
        case COVER_INTERMEDIATE: return "intermediate";
        case COVER_OPENED: return "opened";
        case COVER_CLOSED: return "closed";
        default: return "undefined";
    }
    return NULL;
}
// statust - text format status
static sl_sock_hresult_e statusth(sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    char buf[BUFSIZ];
    dome_status_t dome_status;
    double lastt = get_dome_status(&dome_status);
    if(sl_dtime() - lastt > STATUS_MAX_AGE) return RESULT_FAIL;
    snprintf(buf, 127, "cover1=%s\ncover2=%s\nangle1=%d\nangle2=%d\nrelay1=%d\nrelay2=%d\nrelay3=%d\nreqtime=%.9f\n",
             textst(dome_status.coverstate[0]), textst(dome_status.coverstate[1]),
             dome_status.encoder[0], dome_status.encoder[1],
             dome_status.relay[0], dome_status.relay[1], dome_status.relay[2],
             lastt);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}
// relay on/off
static sl_sock_hresult_e relays(int Nrelay, int Stat){
    dome_cmd_t cmd = Stat ? DOME_RELAY_ON : DOME_RELAY_OFF;
    if(DOME_S_ERROR == dome_poll(cmd, Nrelay)) return RESULT_FAIL;
    return RESULT_OK;
}
static sl_sock_hresult_e relay(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    char buf[128];
    int N = item->key[sizeof(CMD_RELAY) - 1] - '0';
    if(N <  NRELAY_MIN|| N > NRELAY_MAX) return RESULT_BADKEY;
    if(!req || !*req){ // getter
        dome_status_t dome_status;
        double lastt = get_dome_status(&dome_status);
        if(sl_dtime() - lastt > STATUS_MAX_AGE) return RESULT_FAIL;
        snprintf(buf, 127, "%s=%d\n", item->key, dome_status.relay[N-1]);
        sl_sock_sendstrmessage(c, buf);
        return RESULT_SILENCE;
    }
    int Stat = *req - '0';
    return relays(N, Stat);
}
// dome open/close/stop
static sl_sock_hresult_e domecmd(dome_cmd_t cmd){
    if(DOME_S_ERROR == dome_poll(cmd, 0)) return RESULT_FAIL;
    return RESULT_OK;
}
static sl_sock_hresult_e opendome(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    return domecmd(DOME_OPEN);
}
static sl_sock_hresult_e closedome(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    return domecmd(DOME_CLOSE);
}
static sl_sock_hresult_e stopdome(_U_ sl_sock_t *c, _U_ sl_sock_hitem_t *item, _U_ const char *req){
    return domecmd(DOME_STOP);
}
// half open/close
static sl_sock_hresult_e halfmove(sl_sock_t *c, sl_sock_hitem_t *item, const char *req){
    char buf[128];
    int N = item->key[sizeof(CMD_HALF) - 1] - '0';
    if(N < 1 || N > 2) return RESULT_BADKEY;
    if(!req || !*req){ // getter
        dome_status_t dome_status;
        double lastt = get_dome_status(&dome_status);
        if(sl_dtime() - lastt > STATUS_MAX_AGE) return RESULT_FAIL;
        int s = dome_status.coverstate[N-1];
        int S = -1;
        switch(s){
            case COVER_OPENED: S = 1; break;
            case COVER_CLOSED: S = 0; break;
            default: break;
        }
        snprintf(buf, 127, "%s=%d\n", item->key, S);
        sl_sock_sendstrmessage(c, buf);
        return RESULT_SILENCE;
    }
    int Stat = *req - '0';
    dome_cmd_t cmd = Stat ? DOME_OPEN_ONE : DOME_CLOSE_ONE;
    green("\n\nstat=%d, cmd=%d, N=%d\n\n", Stat, cmd, N);
    if(DOME_S_ERROR == dome_poll(cmd, N)) return RESULT_FAIL;
    return RESULT_OK;
}

//  and all handlers collection
static sl_sock_hitem_t handlers[] = {
    {dtimeh, CMD_UNIXT, "get server's UNIX time", NULL},
    {statush, CMD_STATUS, "get dome's status in old format", NULL},
    {statusth, CMD_STATUST, "get dome's status in full text format", NULL},
    {relay, CMD_RELAY "1", "turn on/off (=1/0) relay 1", NULL},
    {relay, CMD_RELAY "2", "turn on/off (=1/0) relay 2", NULL},
    {relay, CMD_RELAY "3", "turn on/off (=1/0) relay 3", NULL},
    {opendome, CMD_OPEN, "open dome", NULL},
    {closedome, CMD_CLOSE, "close dome", NULL},
    {stopdome, CMD_STOP, "stop moving", NULL},
    {halfmove, CMD_HALF "1", "open/close (=1/0) north half of dome", NULL},
    {halfmove, CMD_HALF "2", "open/close (=1/0) south half of dome", NULL},
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
// new connections handler
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

void server_run(sl_socktype_e type, const char *node, sl_tty_t *serial){
    if(!node || !serial){
        LOGERR("server_run(): wrong parameters");
        ERRX("server_run(): wrong parameters");
    }
    dome_serialdev(serial);
    s = sl_sock_run_server(type, node, -1, handlers);
    if(!s) ERRX("Can't create socket and/or run threads");
    sl_sock_changemaxclients(s, 5);
    sl_sock_maxclhandler(s, toomuch);
    sl_sock_connhandler(s, connected);
    sl_sock_dischandler(s, disconnected);
    while(s && s->connected){
        if(!s->rthread){
            LOGERR("Server handlers thread is dead");
            break;
        }
        // finite state machine polling
        dome_poll(DOME_POLL, 0);
    }
    sl_sock_delete(&s);
    ERRX("Server handlers thread is dead");
}
