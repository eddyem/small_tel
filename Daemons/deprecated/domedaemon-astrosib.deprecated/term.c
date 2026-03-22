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
#include "usefull_macros.h"
#include "term.h"
#include <strings.h> // strncasecmp
#include <time.h>    // time(NULL)
#include <limits.h>  // INT_MAX, INT_MIN

#define BUFLEN 1024

static char buf[BUFLEN];

/**
 * read strings from terminal (ending with '\n') with timeout
 * @return NULL if nothing was read or pointer to static buffer
 */
static char *read_string(){
    //FNAME();
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
        if((l = read_tty(ptr, LL))){
            r += l; LL -= l; ptr += l;
            //DBG("got: %s", buf);
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
 * Try to connect to `device` at BAUD_RATE speed
 * @return connection speed if success or 0
 */
void try_connect(char *device){
    if(!device) return;
    char tmpbuf[4096];
    fflush(stdout);
    tty_init(device);
    read_tty(tmpbuf, 4096); // clear rbuf
    putlog("Connected to %s", device);
    DBG("connected");
}

static void con_sig(int rb){
    static char buf[256];
    static int L = 0;
    if(rb < 1) return;
    if(rb != '\n'){
        if(rb == 127 && L > 0){
            printf("\b \b");
            fflush(stdout);
            --L;
        }else{
            if(1 != write(1, &rb, 1))
                printf("%c", (char)rb);
            buf[L++] = (char)rb;
            if(L >= 250){
                printf("\nbuffer overrun!\n");
                L = 0;
            }
        }
    }else{
        //buf[L++] = '\r';
        //buf[L++] = '\n';
        buf[L] = 0;
        printf("\n\t\tSend %s\n", buf);
        write_tty(buf, L);
        L = 0;
    }
}

/**
 * run terminal emulation: send user's commands and show answers
 */
void run_terminal(){
    green(_("Work in terminal mode without echo\n"));
    int rb;
    char buf[BUFLEN];
    size_t l;
    setup_con();
    while(1){
        if((l = read_tty(buf, BUFLEN - 1))){
            buf[l] = 0;
            printf("%s", buf);
        }
        if((rb = read_console())){
            con_sig(rb);
        }
    }
}

/**
 * write command
 * @return answer or NULL if error occured (or no answer)
 */
char *write_cmd(const char *cmd){
    DBG("Write %s", cmd);
    char buf[128];
    snprintf(buf, 128, "%s\r", cmd);
    if(write_tty(buf, strlen(buf))) return NULL;
    double t0 = dtime();
    static char *ans;
    while(dtime() - t0 < T_POLLING_TMOUT){ // read answer
        if((ans = read_string())){ // parse new data
            DBG("got answer: %s", ans);
            return ans;
        }
    }
    return NULL;
}

/**
 * Poll serial port for new dataportion
 * @return: NULL if no data received, pointer to string if valid data received
 */
char *poll_device(){
    char *ans;
    const char *cmdstat = "STATUS";
#define statlen (6)
    static char pollbuf[256];
    while(read_string()); // clear receiving buffer
    if(!(ans = write_cmd(cmdstat))) return NULL; // error writing command
    if(strncmp(ans, cmdstat, statlen)) return NULL; // wrong answer
    ans += statlen;
    char *comma = ans;
    for(int i = 0; i < 4; ++i){
        char *nxtcomma = strchr(comma, ',');
        if(!nxtcomma) return NULL; // wrong answer
        comma = nxtcomma + 1;
    }
    comma[-1] = 0;
    strncpy(pollbuf, ans, 255);
    return pollbuf;
}
