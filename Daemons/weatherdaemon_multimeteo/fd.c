/*
 * This file is part of the weatherdaemon project.
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

// try to open device or socket that user pointed for plugin
// WARNING!!! The `getFD` function intended for single use for each plugin!
// WARNING!!! If you will try to close some plugins in running mode and open others, it
// WARNING!!!     would cause to a memory leak!

#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>  // unix socket
#include <usefull_macros.h>

#include "fd.h"

/**
 * @brief openserial - try to open serial device
 * @param path - path to device and speed, colon-separated (without given speed assume 9600)
 * @return -1 if failed or opened FD
 * WARNING!!! Memory leakage danger. Don't call this function too much times!
 */
static int openserial(char *path){
    FNAME();
    int speed = 9600; // default speed
    char *colon = strchr(path, ':');
    if(colon){
        *colon++ = 0;
        if(!sl_str2i(&speed, colon)){
            WARNX("Wrong speed settings: '%s'", colon);
            return -1;
        }
    }
    sl_tty_t *serial = sl_tty_new(path, speed, BUFSIZ);
    if(!serial || !sl_tty_open(serial, TRUE)){
        WARN("Can't open %s @ speed %d", path, speed);
        return -1;
    }
    return serial->comfd;
}

static char *convunsname(const char *path){
    char *apath = MALLOC(char, 106);
    if(*path == 0 || *path == '@'){
        DBG("convert name starting from 0 or @");
        apath[0] = 0;
        strncpy(apath+1, path+1, 104);
    }else if(strncmp("\\0", path, 2) == 0){
        DBG("convert name starting from \\0");
        apath[0] = 0;
        strncpy(apath+1, path+2, 104);
    }else strncpy(apath, path, 105);
    return apath;
}

/**
 * @brief opensocket - try to open socket
 * @param sock - UNIX socket path or hostname:port for INET socket
 * @param type - UNIX or INET
 * @return -1 if failed or opened FD
 */
static int opensocket(char *path, sl_socktype_e type){
    FNAME();
    DBG("path: '%s'", path);
    int sock = -1;
    struct addrinfo ai = {0}, *res = &ai;
    struct sockaddr_un unaddr = {0};
    char *node = path, *service = NULL;
    ai.ai_socktype = SOCK_STREAM;
    switch(type){
    case SOCKT_UNIX:
        {
        char *str = convunsname(path);
        if(!str) return -1;
        unaddr.sun_family = AF_UNIX;
        ai.ai_addr = (struct sockaddr*) &unaddr;
        ai.ai_addrlen = sizeof(unaddr);
        memcpy(unaddr.sun_path, str, 106);
        FREE(str);
        ai.ai_family = AF_UNIX;
        //ai.ai_socktype = SOCK_SEQPACKET;
        }
        break;
    case SOCKT_NET:
    case SOCKT_NETLOCAL:
        //ai.ai_socktype = SOCK_DGRAM;
        ai.ai_family = AF_INET;
        char *delim = strchr(path, ':');
        if(delim){
            *delim = 0;
            service = delim+1;
            if(delim == path) node = NULL; // only port
        }
        DBG("node: '%s', service: '%s'", node, service);
        int e = getaddrinfo(node, service, &ai, &res);
        if(e){
            WARNX("getaddrinfo(): %s", gai_strerror(e));
            return -1;
        }
        for(struct addrinfo *p = res; p; p = p->ai_next){
            if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) continue;
            DBG("Try proto %d, type %d", p->ai_protocol, p->ai_socktype);
            if(connect(sock, p->ai_addr, p->ai_addrlen) == -1){
                WARN("connect()");
                close(sock); sock = -1;
            } else break;
        }
        break;
    default: // never reached
        WARNX("Unsupported socket type %d", type);
        return -1;
    }
    DBG("FD: %d", sock);
    return sock;
}

/**
 * @brief getFD - try to open given device/socket
 * @param path - rest of string for --plugin= (e.g. "N:host.com:12345")
 *          WARNING!!! Contents of `path` would be modified in this function!
 * @return opened file descriptor or -1 in case of error
 */
int getFD(char *path){
    if(!path || !*path) return -1;
    char type = *path;
    path += 2;
    switch(type){
        case 'D': // serial device
            return openserial(path);
        case 'N': // INET socket
            return opensocket(path, SOCKT_NET);
        case 'U': // UNIX socket
            return opensocket(path, SOCKT_UNIX);
    }
    WARNX("Wrong plugin format: '%c', should be 'D', 'N' or 'U'", type);
    return -1;
}
