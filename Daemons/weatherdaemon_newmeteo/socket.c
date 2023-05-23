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
#include "stat.h"
#include "term.h"

// temporary buffers
#define BUFLEN    (1024)
// Max amount of connections
#define BACKLOG   (30)

extern glob_pars *GP;

static weather_t lastweather = {0};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static weatherstat_t wstat;

typedef enum{
    FORMAT_ERROR,       // send user an error message
    FORMAT_CURDFULL,    // param=value for current data
    FORMAT_CURDSHORT,   // comma-separated current data
    FORMAT_STATFULL,    // param=value for stat
    FORMAT_STATSHORT    // comma-separated stat
} format_t;



/**************** SERVER FUNCTIONS ****************/
/**
 * Send data over socket
 * @param sock      - socket fd
 * @param webquery  - ==1 if this is web query
 * @param format    - data format
 * @return 1 if all OK
 */
static int send_data(int sock, int webquery, format_t format){
    char tbuf[BUFSIZ]; // buffer to send
    char databuf[BUFSIZ]; // buffer with data
    ssize_t Len = 0;
    const char *eol = webquery ? "\r\n" : "\n";
    // fill buffer with data
    pthread_mutex_lock(&mutex);
    if(format == FORMAT_CURDFULL){ // full format
        Len = snprintf(databuf, BUFSIZ,
                       "Wind=%.1f%sDir=%.1f%sPressure=%.1f%sTemperature=%.1f%sHumidity=%.1f%s"
                "Rain=%.1f%sTime=%.3f%s",
                lastweather.windspeed, eol, lastweather.winddir, eol, lastweather.pressure, eol,
                lastweather.temperature, eol, lastweather.humidity, eol, lastweather.rainfall, eol,
                lastweather.tmeasure, eol
        );
    }else if(format == FORMAT_CURDSHORT){ // short format
        Len = snprintf(databuf, BUFSIZ,
                "%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f%s",
                lastweather.tmeasure, lastweather.windspeed, lastweather.winddir,
                lastweather.pressure, lastweather.temperature, lastweather.humidity,
                lastweather.rainfall, eol
        );
    }else if(format == FORMAT_STATFULL){
        char *ptr = databuf;
        int l = BUFSIZ;
#define PRSTAT(field, nm) do{register int lc = snprintf(ptr, l, \
                nm "max=%.1f%s" nm "min=%.1f%s" nm "mean=%.1f%s" nm "rms=%.1f%s", \
                wstat.field.max, eol, wstat.field.min, eol, wstat.field.mean, eol, wstat.field.rms, eol); \
                Len += lc; l -= lc; ptr += lc;}while(0)
        PRSTAT(windspeed, "Wind");
        PRSTAT(winddir, "Dir");
        PRSTAT(pressure, "Pressure");
        PRSTAT(temperature, "Temperature");
        PRSTAT(humidity, "Humidity");
        PRSTAT(rainfall, "Rain");
        PRSTAT(tmeasure, "Time");
#undef PRSTAT
    }else if(format == FORMAT_STATSHORT){
        char *ptr = databuf;
        int l = BUFSIZ;
        register int lc;
#define PRSTAT(field, nm) do{lc = snprintf(ptr, l, \
                "%.1f,%.1f,%.1f,%.1f", \
                wstat.field.max, wstat.field.min, wstat.field.mean, wstat.field.rms); \
                Len += lc; l -= lc; ptr += lc;}while(0)
#define COMMA() do{lc = snprintf(ptr, l, ","); Len += lc; l -= lc; ptr += lc;}while(0)
        PRSTAT(windspeed, "Wind"); COMMA();
        PRSTAT(winddir, "Dir"); COMMA();
        PRSTAT(pressure, "Pressure"); COMMA();
        PRSTAT(temperature, "Temperature"); COMMA();
        PRSTAT(humidity, "Humidity"); COMMA();
        PRSTAT(rainfall, "Rain"); COMMA();
        PRSTAT(tmeasure, "Time");
        Len += snprintf(ptr, l, "%s", eol);
#undef PRSTAT
    }else{
        Len = snprintf(databuf, BUFSIZ, "Error!");
    }
    pthread_mutex_unlock(&mutex);
    // OK buffer ready, prepare to send it
    if(webquery){
        ssize_t L = snprintf(tbuf, BUFSIZ,
            "HTTP/2.0 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST\r\n"
            "Access-Control-Allow-Credentials: true\r\n"
            "Content-type: text/plain\r\nContent-Length: %zd\r\n\r\n", Len);
        if(L < 0){
            WARN("sprintf()");
            LOGWARN("sprintf()");
            return FALSE;
        }
        if(L != write(sock, tbuf, L)){
            LOGWARN("Can't write header");
            WARN("write");
            return FALSE;
        }
    }
    if(Len != write(sock, databuf, Len)){
        WARN("write()");
        LOGERR("send_data(): write() failed");
        return FALSE;
    }
    //LOGDBG("fd %d, write %s", sock, textbuf);
    return TRUE;
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

static double getpar(const char *s){
    double x = -1.;
    char *eptr = NULL;
    while(*s && *s <= ' ') ++s;
    x = strtod(s, &eptr);
    if(eptr == s) x = -1.;
    return x;
}

/**
 * @brief handle_socket - read information from socket
 * @param sock - socket fd
 * @param chkheader - ==1 on first run
 * @return 1 if socket closed
 */
static int handle_socket(int sock){
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
    if(rd <= 0){ // error
        LOGWARN("Client %d close socket on error", sock);
        DBG("Nothing to read from fd %d (ret: %zd)", sock, rd);
        return 1;
    }
    // add trailing zero to be on the safe side
    buff[rd] = 0;
    // now we should check what do user want
    char *found = buff;
    DBG("user send: %s", buff);
    format_t format = FORMAT_CURDFULL; // text format - default for web-queries

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
    }else if(0 == strncmp(buff, "simple", 6)) format = FORMAT_CURDSHORT; // simple format
    else if(0 == strncmp(buff, "stat", 4)){ // show stat
        double dt = -1.; int l = 4;
        if(0 == strncmp(buff, "statsimple", 10)){
            l = 10; format = FORMAT_STATSHORT;
        }else format = FORMAT_STATFULL;
        dt = getpar(buff + l);
        if(dt < 1.) dt = 900.; // 15 minutes - default
        if(stat_for(dt, &wstat) < 1.) format = FORMAT_ERROR;
    }

    // here we can process user data
    DBG("found=%s", found);
    LOGDBG("sockfd=%d, got %s", sock, buff);
    DBG("format=%d", format);
    send_data(sock, webquery, format);
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
    memset(poll_set, 0, sizeof(poll_set));
    poll_set[0].fd = sock;
    poll_set[0].events = POLLIN;
    while(1){
        poll(poll_set, nfd, 1); // poll for 1ms
        for(int fdidx = 0; fdidx < nfd; ++fdidx){ // poll opened FDs
            if((poll_set[fdidx].revents & POLLIN) == 0) continue;
            poll_set[fdidx].revents = 0;
            if(fdidx){ // client
                int fd = poll_set[fdidx].fd;
                if(handle_socket(fd)){ // socket closed - remove it from list
                    close(fd);
                    DBG("Client with fd %d closed", fd);
                    LOGMSG("Client %d disconnected", fd);
                    // move last to free space
                    poll_set[fdidx] = poll_set[nfd - 1];
                    --nfd;
                }
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
                    int _U_ x = write(newsock, "Max amount of connections reached!\n", 35);
                    WARNX("Limit of connections reached");
                    close(newsock);
                }else{
                    memset(&poll_set[nfd], 0, sizeof(struct pollfd));
                    poll_set[nfd].fd = newsock;
                    poll_set[nfd].events = POLLIN;
                    ++nfd;
                }
            }
        } // endfor
    } // endwhile(1)
    LOGERR("server(): UNREACHABLE CODE REACHED!");
}

// poll last weather data
static void *weatherpolling(_U_ void *notused){
    while(1){
        weather_t w;
        if(getlastweather(&w) && 0 == pthread_mutex_lock(&mutex)){
            memcpy(&lastweather, &w, sizeof(weather_t));
            addtobuf(&w);
            pthread_mutex_unlock(&mutex);
        }
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
    if(pthread_create(&parser_thread, NULL, weatherpolling, NULL)){
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
            if(pthread_create(&parser_thread, NULL, weatherpolling, NULL)){
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

