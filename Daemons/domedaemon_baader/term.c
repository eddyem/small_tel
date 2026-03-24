/*
 * This file is part of the baader_dome project.
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

#include <pthread.h>
#include <string.h>
#include <usefull_macros.h>

#include "term.h"

static sl_tty_t *dev = NULL; // shoul be global to restore if die
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// close serial device
void term_close(){
    if(dev) sl_tty_close(&dev);
}

/**
 * @brief open_term - open serial device
 * @param path - path to device
 * @param speed - its speed
 * @param usec - timeout (us), if < 1e-6 - leave default
 * @return FALSE if failed
 */
int term_open(char *path, int speed, double usec){
    pthread_mutex_lock(&mutex);
    if(dev) sl_tty_close(&dev);
    LOGMSG("Try to open serial %s at speed %d", path, speed);
    DBG("Init serial");
    dev = sl_tty_new(path, speed, 4096);
    DBG("Open serial");
    if(dev) dev = sl_tty_open(dev, 1);
    if(!dev) goto rtn;
    DBG("Opened");
    if(usec >= 1e-6){
        DBG("set timeout to %gus", usec);
        if(sl_tty_tmout(usec) < 0){
            LOGWARN("Can't set timeout to %gus", usec);
            WARNX("Can't set timeout to %gus", usec);
        }
    }
    if(speed != dev->speed){
        LOGWARN("Can't set exact speed! Opened %s at speed %d", dev->portname, dev->speed);
        WARNX("Can't set speed %d (try %d)", speed, dev->speed);
    }
    pthread_mutex_unlock(&mutex);
rtn:
    if(dev) return TRUE;
    LOGERR("Can't open %s with speed %d. Exit.", path, speed);
    return FALSE;
}

/**
 * @brief nonblk_read - internal read
 * @param ans (o) - buffer for data
 * @return NULL if failed or `ans`
 */
static char *nonblk_read(char ans[ANSLEN]){
    static char *bufptr = NULL; // last message, if it was longer than `length`
    static int lastL = 0;
    FNAME();
    if(!dev) return NULL;
    int length = ANSLEN - 1;
    DBG("ok");
    if(!ans){ // clear
        DBG("clr");
        while(sl_tty_read(dev) > 0);
        bufptr = NULL;
        lastL = 0;
        return NULL;
    }
    if(bufptr && lastL){
        if(length > lastL) length = lastL;
        memcpy(ans, bufptr, length);
        if((lastL -= length) < 1){
            lastL = 0; bufptr = NULL;
        }
        ans[length] = 0;
        DBG("got %d bytes from old record: '%s'", length, ans);
        return ans;
    }
    int totlen = 0;
    while(length > 0 && sl_tty_read(dev) > 0){
        int lmax = length;
        if(lmax >= (int)dev->buflen){
            DBG("Full buffer can  be copied");
            lmax = (int)dev->buflen;
        }else{ // store part of data in buffer
            lastL = dev->buflen - lmax;
            DBG("Store %d bytes for next read", lastL);
            bufptr = dev->buf + lmax;
        }
        memcpy(ans + totlen, dev->buf, lmax);
        length -= lmax;
        totlen += lmax;
        ans[totlen] = 0;
    }
    DBG("totlen: %d", totlen);
    if(totlen == 0) return NULL;
    DBG("copied %d: '%s'", totlen, ans);
    return ans;
}

/**
 * @brief nonblk_write - internal write
 * @param buf (i) - data to write
 * @param length - its length
 * @return 0 if failed or `length`
 */
static int nonblk_write(const char *buf, int length){
    if(!buf || length < 1) return 0;
    if(0 == sl_tty_write(dev->comfd, buf, length)) return length;
    return 0;
}

/**
 * @brief term_read - read string from terminal
 * @param ans (o) - buffer or NULL to clear last data
 * @return NULL or pointer to zero-terminated `ans`
 */
char *term_read(char ans[ANSLEN]){
    pthread_mutex_lock(&mutex);
    char *ret = nonblk_read(ans);
    pthread_mutex_unlock(&mutex);
    DBG("read: '%s'", ret);
    return ret;
}

/**
 * @brief term_write - write data and got answer
 * @param str (i) - zero-terminated string to write
 * @param ans (o) - NULL to clear incoming data or buffer to read
 * @return
 */
char *term_write(const char *str, char ans[ANSLEN]){
    if(!str || !*str) return NULL;
    DBG("Send cmd '%s'", str);
    char *ret = NULL;
    pthread_mutex_lock(&mutex);
    if(nonblk_write(str, strlen(str))){
        usleep(USLEEP_BEFORE_READ);
        ret = nonblk_read(ans);
    }
    pthread_mutex_unlock(&mutex);
    DBG("read: '%s'", ret);
    return ret;
}

/**
 * @brief term_cmdwans - write command and get answer
 * @param str (i) - command to write
 * @param prefix (i) - prefix of answer (string should begin with this text)
 * @param ans (o) - buffer for answer (non-NULL!!!)
 * @return pointer to data just after `prefix` in `ans` or NULL if no `prefix` found
 */
char *term_cmdwans(const char *str, const char *prefix, char ans[ANSLEN]){
    if(!str || !prefix || !ans) return NULL;
    DBG("Send cmd '%s'", str);
    char *ret = NULL;
    pthread_mutex_lock(&mutex);
    if(nonblk_write(str, strlen(str))){
        usleep(USLEEP_BEFORE_READ);
        ret = nonblk_read(ans);
    }
    pthread_mutex_unlock(&mutex);
    int l = strlen(prefix);
    DBG("compare %s with %s", ret ? (ret) : "null", prefix);
    if(!ret || strncmp(ans, prefix, l)) return NULL; // no answer or not found
    DBG("found");
    return ans + l;
}
