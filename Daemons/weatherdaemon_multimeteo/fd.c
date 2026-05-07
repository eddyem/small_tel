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

/**
 * @brief openserial - try to open serial device
 * @param path - path to device and speed, colon-separated (without given speed assume 9600)
 * @return -1 if failed or opened FD
 * WARNING!!! Memory leakage danger. Don't call this function too much times!
 */
static int openserial(const char *path){
    FNAME();
    int speed = 9600; // default speed
    char *str = strdup(path);
    char *colon = strchr(str, ':');
    if(colon){
        *colon++ = 0;
        if(!sl_str2i(&speed, colon)){
            WARNX("Wrong speed settings: '%s'", colon);
            FREE(str);
            return -1;
        }
    }
    sl_tty_t *serial = sl_tty_new(str, speed, BUFSIZ);
    if(!serial || !sl_tty_open(serial, TRUE)){
        WARN("Can't open %s @ speed %d", str, speed);
        FREE(str);
        return -1;
    }
    DBG("Opened %s @ %d", str, speed);
    FREE(str);
    int comfd = serial->comfd;
    FREE(serial->portname);
    FREE(serial->buf);
    FREE(serial->format);
    FREE(serial);
    return comfd;
}

/**
 * @brief getFD - try to open given device/socket
 * @param path - rest of string for --plugin= (e.g. "N:host.com:12345")
 *          WARNING!!! Contents of `path` would be modified in this function!
 * @return opened file descriptor or -1 in case of error
 */
int getFD(const char *path){
    if(!path || !*path || strlen(path) < 2) return -1;
    char type = *path;
    if(path[1] != ':') return -1; // after protocol letter should be delimeter
    path += 2;
    if(!*path) return -1; // empty path
    switch(type){
        case 'D': // serial device
            return openserial(path);
        case 'N': // INET socket
            //return opensocket(path, SOCKT_NET);
            return sl_sock_open(SOCKT_NET, path, 0, 0);
        case 'U': // UNIX socket
            //return opensocket(path, SOCKT_UNIX);
            return sl_sock_open(SOCKT_UNIX, path, 0, 0);
    }
    WARNX("Wrong plugin format: '%c', should be 'D', 'N' or 'U'", type);
    return -1;
}
