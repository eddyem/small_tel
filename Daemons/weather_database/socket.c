/*
 * This file is part of the sqlite project.
 * Copyright 2023 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <usefull_macros.h>

#include "socket.h"
#include "sql.h"

/**
 * @brief open_socket - open client socket
 * @param server - server IP or name
 * @param port - current port
 * @return socket FD or <0 if error
 */
int open_socket(const char *server, const char *port){
    if(!server || !port) return -1;
    DBG("server: %s, port: %s", server, port);
    int sock = -1;
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if(getaddrinfo(server, port, &hints, &res) != 0){
        WARN("getaddrinfo");
        return -1;
    }
    for(struct addrinfo *p = res; p; p = p->ai_next){
        if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0){ // or SOCK_STREAM?
            LOGWARN("socket()");
            WARN("socket()");
            continue;
        }
        if(connect(sock, p->ai_addr, p->ai_addrlen) == -1){
            LOGWARN("connect()");
            WARN("connect()");
            close(sock); sock = -1;
        }
        break;
    }
    freeaddrinfo(res);
    return sock;
}

// simple wrapper over write: add missed newline and log data
static void sendmessage(int fd, const char *msg, int l){
    if(fd < 1 || !msg || l < 1) return;
    DBG("send to fd %d: %s [%d]", fd, msg, l);
    char *tmpbuf = MALLOC(char, l+1);
    memcpy(tmpbuf, msg, l);
    if(msg[l-1] != '\n') tmpbuf[l++] = '\n';
    ssize_t s = send(fd, tmpbuf, l, MSG_NOSIGNAL);
    if(l != s){
        if(s < 0){
            LOGERR("Server disconnected!");
            ERR("Disconnected");
        }
        LOGWARN("write()");
        WARN("write()");
    }else{
        if(globlog){ // logging turned ON
            tmpbuf[l-1] = 0; // remove trailing '\n' for logging
            LOGMSG("SEND to fd %d: %s", fd, tmpbuf);
        }
    }
    FREE(tmpbuf);
}
static void sendstrmessage(int fd, const char *msg){
    if(fd < 1 || !msg) return;
    int l = strlen(msg);
    sendmessage(fd, msg, l);
}

/**
 * check data from  fd (polling function for client)
 * @param fd - file descriptor
 * @return 0 in case of timeout, 1 in case of fd have data, -1 if error
 */
static int canberead(int fd){
    fd_set fds;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    do{
        int rc = select(fd+1, &fds, NULL, NULL, &timeout);
        if(rc < 0){
            if(errno != EINTR){
                LOGWARN("select()");
                WARN("select()");
                return -1;
            }
            continue;
        }
        break;
    }while(1);
    if(FD_ISSET(fd, &fds)){
        //DBG("FD_ISSET");
        return 1;
    }
    return 0;
}

// collect data and write into database
// @return FALSE if can't get full data string
static int getdata(int fd){
    double t0 = dtime();
    char buf[BUFSIZ];
    int len = 0, leave = BUFSIZ, got = 0;
    while(dtime() - t0 < ANS_TIMEOUT){
        int r = canberead(fd);
        if(r == 0) continue;
        r = read(fd, buf + len, leave);
        if(r < 0){
            LOGERR("Server died");
            signals(1);
        }
        len += r; leave -= r;
        if(buf[len-1] == '\n'){
            buf[--len] = 0;
            got = 1;
            break;
        }
    }
    if(!got) return FALSE; // bad data
    //green("got: %s\n", buf);
    weatherstat_t w;
    if(28 != sscanf(buf, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                    &w.windspeed.max, &w.windspeed.min, &w.windspeed.mean, &w.windspeed.rms,
                    &w.winddir.max, &w.winddir.min, &w.winddir.mean, &w.winddir.rms,
                    &w.pressure.max, &w.pressure.min, &w.pressure.mean, &w.pressure.rms,
                    &w.temperature.max, &w.temperature.min, &w.temperature.mean, &w.temperature.rms,
                    &w.humidity.max, &w.humidity.min, &w.humidity.mean, &w.humidity.rms,
                    &w.rainfall.max, &w.rainfall.min, &w.rainfall.mean, &w.rainfall.rms,
                    &w.tmeasure.max, &w.tmeasure.min, &w.tmeasure.mean, &w.tmeasure.rms)){
        LOGWARN("Bad answer from server: %s", buf);
        return FALSE;
    }
    //printf("max wind: %g\n", w.windspeed.max);
    addtodb(&w);
    return TRUE;
}

/**
 * @brief run_socket run main socket process
 * @param fd - socket file descripto
 */
void run_socket(int fd){
    double t0 = 0.;
    while(1){
        double tlast = dtime();
        if(tlast - t0 >= POLLING_INTERVAL){
            sendstrmessage(fd, SERVER_COMMAND);
            if(getdata(fd)) t0 = tlast;
        }
        usleep(10000);
    }
}
