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
#define CMD_UNIXT       "unixt"
#define CMD_STATUS      "status"
#define CMD_RELAY       "relay"
#define CMD_OPEN        "open"
#define CMD_CLOSE       "close"
#define CMD_STOP        "stop"
#define CMD_HALF        "half"

// main command socket
static sl_sock_t *cmd_socket = NULL;
// socket for stellarium purposes
static int stellarium_sockfd = -1;
// sleep time (us)
static unsigned int sleept = DEFAULT_SLEEP_T;
// running flag
static volatile bool isrunning = false;

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
    snprintf(buf, BUFSIZ-1, "%s=%s\n", item->key, mount_status_str());
    LOGDBG("Client %d asks status: %s", c->fd, buf);
    sl_sock_sendstrmessage(c, buf);
    return RESULT_SILENCE;
}

//  and all handlers collection
static sl_sock_hitem_t handlers[] = {
    {dtimeh, CMD_UNIXT, "get server's UNIX time", NULL},
    {status, CMD_STATUS, "get mount status", NULL},
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
    if(!mount_connect()){
        LOGERR("Can't connect to mount");
        sl_sock_delete(&cmd_socket);
        return;
    }
    isrunning = true;
    DBG("While");
    while(isrunning && cmd_socket && cmd_socket->connected){
        usleep(sleept);
        if(!cmd_socket->rthread){
            LOGERR("Server handlers thread is dead");
            break;
        }
        // finite state machine polling
        //dome_poll(DOME_POLL, 0);
    }
    DBG("Stop command socket");
    sl_sock_delete(&cmd_socket);
    WARNX("Server is dead");
    LOGERR("Server is dead");
    isrunning = false;
}

void server_stop(){
    isrunning = false;
}
