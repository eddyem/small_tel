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

#include <ctype.h>  // isspace
#include <stdio.h>
#include <string.h>
#include <strings.h> // strncasecmp
#include <time.h>    // time(NULL)
#include <limits.h>  // INT_MAX, INT_MIN
#include <pthread.h>

#include "cmdlnopts.h"
#include "term.h"

#define BUFLEN (4096)

static sl_tty_t *ttydescr = NULL;
static int emulate = FALSE;

static char buf[BUFLEN];
static const char *emultemplate = "0R0,S=1.9,D=217.2,P=787.7,T=10.8,H=69.0,R=31.0,Ri=0.0,Rs=Y";

/**
 * read strings from terminal (ending with '\n') with timeout
 * @return NULL if nothing was read or pointer to static buffer
 */
static char *read_string(){
    //static int done = 0;
    if(emulate){
        strncpy(buf, emultemplate, BUFLEN);
        return buf;
    }
    if(!ttydescr) ERRX("Serial device not initialized");
    size_t r = 0, l;
    int LL = BUFLEN - 1;
    char *ptr = buf;
    double d0 = sl_dtime();
    do{
        if((l = sl_tty_read(ttydescr))){
            strncpy(ptr, ttydescr->buf, LL);
            r += l; LL -= l; ptr += l;
            //DBG("l=%zd, r=%zd, LL=%d", l, r, LL);
            d0 = sl_dtime();
            if(r > 2 && ptr[-1] == '\n') break;
        }
    }while(sl_dtime() - d0 < WAIT_TMOUT && LL);
    if(r){
        //buf[r] = 0;
        //DBG("buf: %s", buf);
        return buf;
    }
    return NULL;
}

/**
 * Try to connect to `device` at baudrate speed
 * @return 1 if OK
 */
int try_connect(char *device, int baudrate, int emul){
    if(emul){
        emulate = TRUE;
        DBG("Emulation mode");
        return TRUE;
    }
    if(!device) return FALSE;
    fflush(stdout);
    ttydescr = sl_tty_new(device, baudrate, 1024);
    if(ttydescr) ttydescr = sl_tty_open(ttydescr, 1); // exclusive open
    if(!ttydescr) return FALSE;
    while(sl_tty_read(ttydescr)); // clear rbuf
    LOGMSG("Connected to %s", device);
    return TRUE;
}

// stop polling thread and close tty
void stop_tty(){
    if(ttydescr)  sl_tty_close(&ttydescr);
}

static weather_t lastweather;
typedef struct{
    const char *parname;
    int parlen;
    double *weatherpar;
} wpair_t;

static const wpair_t wpairs[] = {
    {"S=", 2, &lastweather.windspeed},
    {"D=", 2, &lastweather.winddir},
    {"P=", 2, &lastweather.pressure},
    {"T=", 2, &lastweather.temperature},
    {"H=", 2, &lastweather.humidity},
    {"R=", 2, &lastweather.rainfall},
    {NULL, 0, NULL}
};

static int parseans(char *str, weather_t *w){
    if(strncmp(str, "0R0,", 4)){
        WARNX("Wrong answer");
        LOGWARN("poll_device() get wrong answer: %s", str);
        return FALSE;
    }
    str += 3;
    do{
        ++str;
        //DBG("start=%s", str);
        const wpair_t *el = wpairs;
        while(el->parname){
            if(strncmp(str, el->parname, el->parlen) == 0){ // found next parameter
                str += el->parlen;
                char *endptr;
                *el->weatherpar = strtod(str, &endptr);
                //DBG("found par: %s, val=%g", el->parname, *el->weatherpar);
                if(endptr == str){
                    DBG("Wrong double value");
                    *el->weatherpar = 0.;
                    //return FALSE;
                }
                break;
            }
            ++el;
        }
        str = strchr(str, ',');
        //DBG("next=%s", str);
    }while(str && *str);
    lastweather.tmeasure = sl_dtime();
    if(w) memcpy(w, &lastweather, sizeof(weather_t));
    return TRUE;
}

// get weather measurements; return FALSE if something failed
int getlastweather(weather_t *w){
    if(!emulate){
        if(sl_tty_write(ttydescr->comfd, "!0R0\r\n", 6))
            return FALSE;
    }
    double t0 = sl_dtime();
    while(sl_dtime() - t0 < T_POLLING_TMOUT){
        char *r = NULL;
        if((r = read_string())){ // parse new data
            //DBG("got %s", r);
            if(parseans(r, w)) return TRUE;
        }
    }
    return FALSE;
}
