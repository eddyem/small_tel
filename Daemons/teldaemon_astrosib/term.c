/*
 * This file is part of the teldaemon project.
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

#include <string.h>
#include <usefull_macros.h>

#include "term.h"

static sl_tty_t *dev = NULL; // shoul be global to restore if die

// close serial device
void close_term(){
    if(dev) sl_tty_close(&dev);
}

/**
 * @brief open_term - open serial device
 * @param path - path to device
 * @param speed - its speed
 * @param usec - timeout (us), if < 1e-6 - leave default
 * @return FALSE if failed
 */
int open_term(char *path, int speed, double usec){
    if(dev) sl_tty_close(&dev);
    LOGMSG("Try to open serial %s at speed %d", path, speed);
    DBG("Open serial");
    dev = sl_tty_new(path, speed, 4096);
    if(dev) dev = sl_tty_open(dev, 1);
    if(!dev){
        LOGERR("Can't open %s with speed %d. Exit.", path, speed);
        return FALSE;
    }
    if(usec >= 1e-6){
        DBG("set timeout to %gus", usec);
        if(!sl_tty_tmout(usec)){
            LOGWARN("Can't set timeout to %gus", usec);
            WARNX("Can't set timeout to %gus", usec);
        }
    }
    if(speed != dev->speed){
        LOGWARN("Can't set exact speed! Opened %s at speed %d", dev->portname, dev->speed);
        WARNX("Can't set speed %d (try %d)", speed, dev->speed);
    }
    if(dev) return TRUE;
    return FALSE;
}

/**
 * @brief read_term - read data from serial terminal
 * @param buf - buffer for data
 * @param length - size of `buf`
 * @return amount of data read
 */
int read_term(char *buf, int length){
    static char *bufptr = NULL; // last message, if it was longer than `length`
    static int lastL = 0;
    if(!dev || !buf || length < 1) return 0;
    if(bufptr && lastL){
        if(length > lastL) length = lastL;
        DBG("got %d bytes from old record", length);
        memcpy(buf, bufptr, length);
        if((lastL -= length) < 1){
            lastL = 0; bufptr = NULL;
        }
        return length;
    }
    if(!sl_tty_read(dev)) return 0;
    DBG("Got from serial %zd bytes", dev->buflen);
    LOGDBG("Got from serial: %zd bytes", dev->buflen);
    if(length >= (int)dev->buflen){
        DBG("Full buffer can  be copied");
        length = (int)dev->buflen;
        bufptr = NULL;
        lastL = 0;
    }else{ // store part of data in buffer
        lastL = dev->buflen - length;
        DBG("Store %d bytes for next read", lastL);
        bufptr = dev->buf + length;
    }
    memcpy(buf, dev->buf, length);
    return length;
}

/**
 * @brief write_term - write data
 * @param buf - buffer
 * @param length - its length
 * @return 0 if OK and 1 if failed
 */
int write_term(const char *buf, int length){
    if(!dev || !buf || length < 1) return 0;
    return sl_tty_write(dev->comfd, buf, length);
}

// write string command
int write_cmd(const char *buf){
    if(!buf || !*buf) return 0;
    DBG("Ask to write %s", buf);
    return write_term(buf, strlen(buf));
}
