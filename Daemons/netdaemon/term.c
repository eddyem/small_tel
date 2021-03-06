/*                                                                                                  geany_encoding=koi8-r
 * client.c - terminal parser
 *
 * Copyright 2018 Edward V. Emelianoff <eddy@sao.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <stdio.h>
#include <string.h>
#include <strings.h> // strncasecmp
#include <time.h>    // time(NULL)
#include <limits.h>  // INT_MAX, INT_MIN
#include "term.h"

#define BUFLEN 1024

TTY_descr *ttydescr = NULL;

static char buf[BUFLEN];

/**
 * read strings from terminal (ending with '\n') with timeout
 * @return NULL if nothing was read or pointer to static buffer
 */
static char *read_string(){
    if(!ttydescr) ERRX("Serial device not initialized");
    size_t r = 0, l;
    int LL = BUFLEN - 1;
    char *ptr = NULL;
    static char *optr = NULL;
    if(optr && *optr){
        ptr = optr;
        optr = strchr(optr, '\n');
        if(optr) ++optr;
        //DBG("got data, roll to next; ptr=%s\noptr=%s",ptr,optr);
        return ptr;
    }
    ptr = buf;
    double d0 = dtime();
    do{
        if((l = read_tty(ttydescr))){
            r += l; LL -= l; ptr += l;
            if(ptr[-1] == '\n') break;
            d0 = dtime();
        }
    }while(dtime() - d0 < WAIT_TMOUT && LL);
    if(r){
        buf[r] = 0;
        //DBG("r=%zd, got string: %s", r, buf);
        optr = strchr(buf, '\n');
        if(optr) ++optr;
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

/**
 * run terminal emulation: send user's commands and show answers
 */
void run_terminal(){
    if(!ttydescr) ERRX("Terminal not connected");
    green(_("Work in terminal mode without echo\n"));
    int rb;
    size_t l;
    setup_con();
    while(1){
        if((l = read_tty(ttydescr))){
            printf("%s", ttydescr->buf);
        }
        if((rb = read_console())){
            char c = (char) rb;
            write_tty(ttydescr->comfd, &c, 1);
        }
    }
}

/**
 * Poll serial port for new dataportion
 * @return: NULL if no data received, pointer to string if valid data received
 */
char *poll_device(){
    char *ans;
    double t0 = dtime();
    while(dtime() - t0 < T_POLLING_TMOUT){
        if((ans = read_string())){ // parse new data
            DBG("got %s", ans);
            /*
             * INSERT CODE HERE
             * (data validation)
             */
            return ans;
        }
    }
    return NULL;
}
