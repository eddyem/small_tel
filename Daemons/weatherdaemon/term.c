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

#include "bta_shdata.h"
#include "cmdlnopts.h"
#include "term.h"

#define BUFLEN (4096)

sl_tty_t *ttydescr = NULL;
extern glob_pars *GP;

static char buf[BUFLEN];
static const char *emultemplate = "<?U> 06:50:36, 20.01.00, TE-2.20, DR1405.50, WU2057.68, RT0.00, WK1.00, WR177.80, WT-2.20, FE0.69, RE0.00, WG7.36, WV260.03, TI0.00, FI0.00,";

/**
 * read strings from terminal (ending with '\n') with timeout
 * @return NULL if nothing was read or pointer to static buffer
 */
static char *read_string(){
    //static int done = 0;
    if(GP->emul){
        //if(done) return NULL;
        strncpy(buf, emultemplate, BUFLEN);
        //done = 1;
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
            DBG("l=%zd, r=%zd, LL=%d", l, r, LL);
            if(ptr[-1] == '\n'){
                DBG("Got newline");
                ptr[-1] = 0;
                break;
            }
            d0 = sl_dtime();
        }
    }while(sl_dtime() - d0 < WAIT_TMOUT && LL);
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
    ttydescr = sl_tty_new(device, baudrate, 1024);
    if(ttydescr) ttydescr = sl_tty_open(ttydescr, 1); // exclusive open
    if(!ttydescr) return 0;
    while(sl_tty_read(ttydescr)); // clear rbuf
    LOGMSG("Connected to %s", device);
    return 1;
}

/**
 * @brief getpar - get parameter value
 * @param string (i) - string where to search
 * @param Val (o)    - value found
 * @param Name       - parameter name
 * @return TRUE if found
 */
static int getpar(char *string, double *Val, char *Name){
    if(!string || !Val || !Name) return FALSE;
    char *p = strstr(string, Name);
    if(!p) return FALSE;
    p += strlen(Name);
    DBG("search %s", Name);
    char *endptr;
    *Val = strtod(p, &endptr);
    DBG("eptr=%s, val=%g", endptr, *Val);
    if(endptr == string){
        WARNX("Double value not found");
        return FALSE;
    }
    return TRUE;
}


/**
 * Poll serial port for new dataportion
 * @return: NULL if no data received, pointer to string if valid data received
 */
char *poll_device(char *ans, int anslen){
    FNAME();
    // gust (>10m/s) time
    static time_t gustt = 0., btagustt = 0.;
    char *ptr = ans, *r = NULL;
    if(!GP->emul){
        if(sl_tty_write(ttydescr->comfd, "?U\r\n", 4))
            return NULL;
    }
    double t0 = sl_dtime();
    while(sl_dtime() - t0 < T_POLLING_TMOUT){
        if((r = read_string())){ // parse new data
            DBG("got %s", r);
            if(strncmp(r, "<?U>", 4)){
                WARNX("Wrong answer");
                LOGWARN("poll_device() get wrong answer: %s", r);
                return NULL;
            }
            r += 4;
            DBG("R=%s", r);
            while(*r){if(isspace(*r)) ++r; else break;}
            DBG("R=%s", r);
            char *eol = strchr(r, '\n');
            if(eol) *eol = 0;
            double d;
            int L = anslen - 1, l;
            time_t tnow = time(NULL);
#define PRINT(...)  do{l = snprintf(ptr, L, __VA_ARGS__); if(l > 0){ L -= l; ptr += l;}}while(0)
            if(getpar(r, &d, "RT")) PRINT("Rain=%g\n", d);
            if(getpar(r, &d, "WU")) PRINT("Clouds=%.1f\n", d);
            if(getpar(r, &d, "TE")) PRINT("Exttemp=%.1f\n", d);
            if(getpar(r, &d, "WG")){
                d /= 3.6;
                PRINT("Wind=%.1f\n", d);
                if(d > GUST_WIND) gustt = tnow;
                if(getpar(r, &d, "WR")) PRINT("Dir=%.1f\n", d);
            }
            if(tnow - gustt < GUST_MAX_TIME) PRINT("Gusttime=%lld\n", (long long)gustt);
            // now get BTA parameters
            if(check_shm_block(&sdat)){
                PRINT("BTAExttemp=%.1f\n", val_T1);
                PRINT("BTAPres=%.1f\n", val_B);
                PRINT("BTAWind=%.1f\n", val_Wnd);
                if(val_Wnd > GUST_WIND) btagustt = tnow;
                if(tnow - btagustt < GUST_MAX_TIME) PRINT("BTAGusttime=%lld\n", (long long)btagustt);
                PRINT("BTAHumid=%.1f\n", val_Hmd);
            }
#undef PRINT
            snprintf(ptr, L, "Time=%lld\n", (long long)tnow);
            DBG("Buffer: %s", ans);
            return ans;
        }
    }
    return NULL;
}

