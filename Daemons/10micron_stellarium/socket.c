/*
 * This file is part of the StelD project.
 * Copyright 2021 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "cmdlnopts.h"
#include "socket.h"
#include "usefull_macro.h"

// max time to wait answer from server
#define WAITANSTIME         (1.0)

static int sockfd = -1; // server file descriptor
static pthread_t sock_thread;
static char buf[BUFSIZ]; // buffer for messages
static int Nread; // amount of bytes in buf
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *getmessages(_U_ void *par);

/**
 * @brief weatherserver_connect - connect to a weather server
 * @return FALSE if failed
 */
int weatherserver_connect(){
    if(sockfd > 0) return TRUE;
    DBG("connect to %s:%d", GP->weathserver, GP->weathport);
    char port[10];
    snprintf(port, 10, "%d", GP->weathport);
    struct addrinfo hints = {0}, *res, *p;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if(getaddrinfo(GP->weathserver, port, &hints, &res) != 0){
        WARN("getaddrinfo()");
        return FALSE;
    }
    // loop through all the results and connect to the first we can
    for(p = res; p; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            WARN("socket");
            continue;
        }
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            WARN("connect()");
            close(sockfd);
            continue;
        }
        break; // if we get here, we have a successfull connection
    }
    if(!p){
        WARNX("Can't connect to socket");
        sockfd = -1;
        return FALSE;
    }
    freeaddrinfo(res);
    if(pthread_create(&sock_thread, NULL, getmessages, NULL)){
        WARN("pthread_create()");
        weatherserver_disconnect();
        return FALSE;
    }
    DBG("connected, fd=%d", sockfd);
    return TRUE;
}

void weatherserver_disconnect(){
    if(sockfd > -1){
        pthread_kill(sock_thread, 9);
        pthread_join(sock_thread, NULL);
        close(sockfd);
    }
    sockfd = -1;
}

/**
 * @brief getparval - return value of parameter
 * @param par (i)        - parameter value
 * @param ansbuf (i)     - buffer with server answer
 * @param val (o)        - value of parameter
 * @return TRUE if parameter found and set `val` to its value
 */
int getparval(const char *par, const char *ansbuf, double *val){
    if(!par || !ansbuf) return FALSE;
    int ret = FALSE;
    char *b = strdup(ansbuf);
    char *parval = NULL, *token = strtok(b, "\n");
    int l = strlen(par);
    if(!token) goto rtn;
    while(token){
        if(strncmp(token, par, l) == 0){ // found
            //DBG("token: '%s'", token);
            parval = strchr(token, '=');
            if(!parval) goto rtn;
            ++parval; while(*parval == ' ' || *parval == '\t') ++parval;
            //DBG("parval: '%s'", parval);
            ret = TRUE;
            break;
        }
        token = strtok(NULL, "\n");
    }
    if(parval && val){
        *val = atof(parval);
        //DBG("Set %s to %g", par, *val);
    }
rtn:
    FREE(b);
    return ret;
}

/**
 * wait for answer from socket
 * @return FALSE in case of error or timeout, TRUE if socket is ready
 */
static int canread(){
    if(sockfd < 0) return FALSE;
    fd_set fds;
    struct timeval timeout;
    int rc;
    // wait not more than 10ms
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);
    do{
        rc = select(sockfd+1, &fds, NULL, NULL, &timeout);
        if(rc < 0){
            if(errno != EINTR){
                WARN("select()");
                return FALSE;
            }
            continue;
        }
        break;
    }while(1);
    if(FD_ISSET(sockfd, &fds)) return TRUE;
    return FALSE;
}

/**
 * @brief getmessages - continuosly read data from server and fill buffer
 */
static void *getmessages(_U_ void *par){
    write(sockfd, "get\n", 4);
    while(sockfd > 0){
        pthread_mutex_lock(&mutex);
        if(Nread == 0){
            double t0 = dtime();
            while(dtime() - t0 < WAITANSTIME && Nread < BUFSIZ){
                if(!canread()) continue;
                int n = read(sockfd, buf+Nread, BUFSIZ-Nread);
                if(n == 0) break;
                if(n < 0){
                    close(sockfd);
                    sockfd = -1;
                    return NULL;
                }
                Nread += n;
            }
            if(Nread){
                buf[Nread] = 0;
                //DBG("got %d: %s", Nread, buf);
            }
        }
        pthread_mutex_unlock(&mutex);
        if(Nread == 0){
            sleep(1);
        }
    }
    return NULL;
}

/**
 * @brief getweathbuffer - read whole buffer with data and set Nread to zero
 * @return NULL if no data or buffer (allocated here)
 */
char *getweathbuffer(){
    if(!weatherserver_connect()) return NULL; // not connected & can't connect
    char *ret = NULL;
    pthread_mutex_lock(&mutex);
    if(Nread){
        ret = strdup(buf);
        Nread = 0;
    }
    pthread_mutex_unlock(&mutex);
    return ret;
}
