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

static TTY_descr *ttydescr = NULL;
extern glob_pars *GP;

static char buf[BUFLEN];
static const char *emultemplate = "0R0,S=1.9,D=217.2,P=787.7,T=10.8,H=69.0,R=31.0,Ri=0.0,Rs=Y";

/**
 * read strings from terminal (ending with '\n') with timeout
 * @return NULL if nothing was read or pointer to static buffer
 */
static char *read_string(){
    //static int done = 0;
    if(GP->emul){
        usleep(100000);
        strncpy(buf, emultemplate, BUFLEN);
        return buf;
    }
    if(!ttydescr) ERRX("Serial device not initialized");
    size_t r = 0, l;
    int LL = BUFLEN - 1;
    char *ptr = buf;
    double d0 = dtime();
    do{
        if((l = read_tty(ttydescr))){
            strncpy(ptr, ttydescr->buf, LL);
            r += l; LL -= l; ptr += l;
            DBG("l=%zd, r=%zd, LL=%d", l, r, LL);
            d0 = dtime();
            if(r > 2 && ptr[-1] == '\n') break;
        }
    }while(dtime() - d0 < WAIT_TMOUT && LL);
    if(r){
        //buf[r] = 0;
        DBG("buf: %s", buf);
        return buf;
    }
    return NULL;
}

/**
 * Try to connect to `device` at baudrate speed
 * @return 1 if OK
 */
int try_connect(char *device, int baudrate){
    if(!device) return 0;
    fflush(stdout);
    ttydescr = new_tty(device, baudrate, 1024);
    if(ttydescr) ttydescr = tty_open(ttydescr, 1); // exclusive open
    if(!ttydescr) return 0;
    while(read_tty(ttydescr)); // clear rbuf
    LOGMSG("Connected to %s", device);
    return 1;
}

// stop polling thread and close tty
void stop_tty(){
    if(ttydescr)  close_tty(&ttydescr);
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
                    return FALSE;
                }
                break;
            }
            ++el;
        }
        str = strchr(str, ',');
        //DBG("next=%s", str);
    }while(str && *str);
    lastweather.tmeasure = dtime();
    if(w) memcpy(w, &lastweather, sizeof(weather_t));
    return TRUE;
}

// get weather measurements; return FALSE if something failed
int getlastweather(weather_t *w){
    if(!GP->emul){
        if(write_tty(ttydescr->comfd, "!0R0\r\n", 6))
            return FALSE;
    }
    double t0 = dtime();
    while(dtime() - t0 < T_POLLING_TMOUT){
        char *r = NULL;
        if((r = read_string())){ // parse new data
            //DBG("got %s", r);
            if(parseans(r, w)) return TRUE;
        }
    }
    return FALSE;
}
