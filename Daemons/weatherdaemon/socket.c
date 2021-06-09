/*
 * This file is part of the weatherdaemon project.
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

#include <netdb.h>      // addrinfo
#include <arpa/inet.h>  // inet_ntop
#include <limits.h>     // INT_xxx
#include <poll.h>       // poll
#include <pthread.h>
#include <signal.h>     // pthread_kill
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h> // syscall
#include <unistd.h>     // daemon
#include <usefull_macros.h>

#include "cmdlnopts.h"   // glob_pars
#include "socket.h"
#include "term.h"

// temporary buffers
#define BUFLEN    (1024)
// Max amount of connections
#define BACKLOG   (30)

extern glob_pars *GP;

static char *answer = NULL;
static int freshdata = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/**************** SERVER FUNCTIONS ****************/
/**
 * Send data over socket
 * @param sock      - socket fd
 * @param webquery  - ==1 if this is web query
 * @param textbuf   - zero-trailing buffer with data to send
 * @return 1 if all OK
 */
static int send_data(int sock, int webquery, char *textbuf){
    ssize_t L, Len;
    char tbuf[BUFLEN];
    Len = strlen(textbuf);
    // OK buffer ready, prepare to send it
    if(webquery){
        L = snprintf((char*)tbuf, BUFLEN,
            "HTTP/2.0 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST\r\n"
            "Access-Control-Allow-Credentials: true\r\n"
            "Content-type: text/plain\r\nContent-Length: %zd\r\n\r\n", Len);
        if(L < 0){
            WARN("sprintf()");
            LOGWARN("sprintf()");
            return 0;
        }
        if(L != write(sock, tbuf, L)){
            LOGWARN("Can't write header");
            WARN("write");
            return 0;
        }
    }
    // send data
    //DBG("send %zd bytes\nBUF: %s", Len, buf);
    if(Len != write(sock, textbuf, Len)){
        WARN("write()");
        LOGERR("send_data(): write() failed");
        return 0;
    }
    //LOGDBG("fd %d, write %s", sock, textbuf);
    return 1;
}

// search a first word after needle without spaces
static char* stringscan(char *str, char *needle){
    char *a;//, *e;
    char *end = str + strlen(str);
    a = strstr(str, needle);
    if(!a) return NULL;
    a += strlen(needle);
    while (a < end && (*a == ' ' || *a == '\r' || *a == '\t' || *a == '\r')) a++;
    if(a >= end) return NULL;
    return a;
}

/**
 * @brief handle_socket - read information from socket
 * @param sock - socket fd
 * @param chkheader - ==1 on first run
 * @return 1 if socket closed
 */
static int handle_socket(int sock, int notchkhdr){
    FNAME();
    int webquery = 0; // whether query is web or regular
    char buff[BUFLEN];
    ssize_t rd;
    if(!(rd = read(sock, buff, BUFLEN-1))){
        //LOGMSG("Client %d closed", sock);
        return 1;
    }
    //LOG("client send %zd bytes", rd);
    DBG("Got %zd bytes", rd);
    if(rd < 0){ // error
        LOGWARN("Client %d close socket on error", sock);
        DBG("Nothing to read from fd %d (ret: %zd)", sock, rd);
        return 1;
    }
    // add trailing zero to be on the safe side
    buff[rd] = 0;
    // now we should check what do user want
    char *found = buff;
    DBG("user send: %s", buff);
    if(!notchkhdr){
        if(0 == strncmp(buff, "GET", 3)){
            DBG("GET");
            // GET web query have format GET /some.resource
            webquery = 1;
            char *slash = strchr(buff, '/');
            if(slash){
                found = slash + 1;
                char *eol = strstr(found, "HTTP");
                if(eol) *eol = 0;
            }
        }else if(0 == strncmp(buff, "POST", 4)){
            DBG("POST");
            webquery = 1;
            // search content length of POST query
            char *cl = stringscan(buff, "Content-Length:");
            if(cl){
                int contlen = atoi(cl);
                int l = strlen(buff);
                if(contlen && l > contlen)  found = &buff[l - contlen];
            }
        }
    }
    // here we can process user data
    DBG("found=%s", found);
    LOGDBG("sockfd=%d, got %s", sock, buff);
    if(GP->echo){
        if(!send_data(sock, webquery, found)){
            LOGWARN("Can't send data, some error occured");
            return 1;
        }
    }
    if(answer) send_data(sock, webquery, answer);
    else send_data(sock, webquery, "No data\n");
    if(webquery) return 1; // close web query after message processing
    return 0;
}

// main socket server
static void *server(void *asock){
    LOGMSG("server()");
    int sock = *((int*)asock);
    if(listen(sock, BACKLOG) == -1){
        LOGERR("listen() failed");
        WARN("listen");
        return NULL;
    }
    int nfd = 1; // current fd amount in poll_set
    struct pollfd poll_set[MAX_FDS];
    int notchkhdr[MAX_FDS];
    memset(poll_set, 0, sizeof(poll_set));
    memset(notchkhdr, 0, sizeof(notchkhdr));
    poll_set[0].fd = sock;
    poll_set[0].events = POLLIN;
    double lastdatat = dtime();
    while(1){
        poll(poll_set, nfd, 1); // poll for 1ms
        for(int fdidx = 0; fdidx < nfd; ++fdidx){ // poll opened FDs
            if((poll_set[fdidx].revents & POLLIN) == 0) continue;
            poll_set[fdidx].revents = 0;
            if(fdidx){ // client
                int fd = poll_set[fdidx].fd;
                if(handle_socket(fd, notchkhdr[fdidx])){ // socket closed - remove it from list
                    close(fd);
                    DBG("Client with fd %d closed", fd);
                    LOGMSG("Client %d disconnected", fd);
                    // move last to free space
                    poll_set[fdidx] = poll_set[nfd - 1];
                    notchkhdr[fdidx] = notchkhdr[nfd - 1];
                    --nfd;
                }else notchkhdr[fdidx] = 1;
            }else{ // server
                socklen_t size = sizeof(struct sockaddr_in);
                struct sockaddr_in their_addr;
                int newsock = accept(sock, (struct sockaddr*)&their_addr, &size);
                if(newsock <= 0){
                    LOGERR("server(): accept() failed");
                    WARN("accept()");
                    continue;
                }
                struct in_addr ipAddr = their_addr.sin_addr;
                char str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN);
                DBG("Connection from %s, give fd=%d", str, newsock);
                LOGMSG("Got connection from %s, fd=%d", str, newsock);
                if(nfd == MAX_FDS){
                    LOGWARN("Max amount of connections: disconnect %s (%d)", str, newsock);
                    send_data(newsock, 0, "Max amount of connections reached!\n");
                    WARNX("Limit of connections reached");
                    close(newsock);
                }else{
                    memset(&poll_set[nfd], 0, sizeof(struct pollfd));
                    poll_set[nfd].fd = newsock;
                    poll_set[nfd].events = POLLIN;
                    notchkhdr[nfd] = 0;
                    ++nfd;
                }
            }
        } // endfor
        if(freshdata && answer){ // send new data to all
            freshdata = 0;
            lastdatat = dtime();
            for(int fdidx = 1; fdidx < nfd; ++fdidx){
                if(notchkhdr[fdidx])
                    send_data(poll_set[fdidx].fd, 0, answer);
            }
        }
        if(dtime() - lastdatat > NODATA_TMOUT){
            LOGERR("No data timeout");
            ERRX("No data timeout");
        }
    }
    LOGERR("server(): UNREACHABLE CODE REACHED!");
}

static void *ttyparser(_U_ void *notused){
    double tlast = 0;
    while(1){
        if(dtime() - tlast > T_INTERVAL){
            char *got = poll_device();
            if(got){
                if (0 == pthread_mutex_lock(&mutex)){
                    FREE(answer);
                    answer = strdup(got);
                    freshdata = 1;
                    pthread_mutex_unlock(&mutex);
                }
                tlast = dtime();
            }
        }
        sleep(1);
    }
    return NULL;
}

// data gathering & socket management
static void daemon_(int sock){
    if(sock < 0) return;
    pthread_t sock_thread, parser_thread;
    if(pthread_create(&sock_thread, NULL, server, (void*) &sock)){
        LOGERR("daemon_(): pthread_create(sock_thread) failed");
        ERR("pthread_create()");
    }
    if(pthread_create(&parser_thread, NULL, ttyparser, NULL)){
        LOGERR("daemon_(): pthread_create(parser_thread) failed");
        ERR("pthread_create()");
    }
    do{
        if(pthread_kill(sock_thread, 0) == ESRCH){ // died
            WARNX("Sockets thread died");
            LOGWARN("Sockets thread died");
            pthread_join(sock_thread, NULL);
            if(pthread_create(&sock_thread, NULL, server, (void*) &sock)){
                LOGERR("daemon_(): new pthread_create() failed");
                ERR("pthread_create()");
            }
        }
        if(pthread_kill(parser_thread, 0) == ESRCH){ // died
            WARNX("TTY thread died");
            LOGWARN("TTY thread died");
            pthread_join(parser_thread, NULL);
            if(pthread_create(&parser_thread, NULL, ttyparser, NULL)){
                LOGERR("daemon_(): new pthread_create(parser_thread) failed");
                ERR("pthread_create()");
            }
        }
        usleep(1000); // sleep a little
    }while(1);
    LOGERR("daemon_(): UNREACHABLE CODE REACHED!");
}

/**
 * Run daemon service
 */
void daemonize(char *port){
    FNAME();
    int sock = -1;
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    // To accept only local sockets replace NULL with "127.0.0.1" and remove AI_PASSIVE
    if(getaddrinfo(NULL, port, &hints, &res) != 0){
        LOGERR("getaddrinfo");
        ERR("getaddrinfo");
    }
    struct sockaddr_in *ia = (struct sockaddr_in*)res->ai_addr;
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ia->sin_addr), str, INET_ADDRSTRLEN);
    // loop through all the results and bind to the first we can
    for(p = res; p != NULL; p = p->ai_next){
        if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            WARN("socket");
            continue;
        }
        int reuseaddr = 1;
        if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1){
            LOGERR("setsockopt() error");
            ERR("setsockopt");
        }
        if(bind(sock, p->ai_addr, p->ai_addrlen) == -1){
            close(sock);
            WARN("bind");
            LOGWARN("bind() error");
            continue;
        }
        break; // if we get here, we have a successfull connection
    }
    if(p == NULL){
        LOGERR("daemonize(): failed to bind socket, exit");
        // looped off the end of the list with no successful bind
        ERRX("failed to bind socket");
    }
    freeaddrinfo(res);
    daemon_(sock);
    close(sock);
    LOGERR("daemonize(): UNREACHABLE CODE REACHED!");
    signals(0);
}

