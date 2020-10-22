/*
 * This file is part of the weatherchk project.
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <string.h> // strcmp
#include <usefull_macros.h>
#include "cmdlnopts.h"

#define ERRCTR_MAX 7
#define BUFLEN 2048

/**
 * @brief getpar - get parameter value
 * @param string (i) - string where to search
 * @param Val (o)    - value found
 * @param Name       - parameter name
 * @return 0 if found
 */
static int getpar(char *string, double *Val, char *Name){
    char *p = strstr(string, Name);
    if(!p) return 1;
    p += strlen(Name);
    DBG("search %s: %s", Name, p);
    if(!Val) return 0;
    char *endptr;
    *Val = strtod(p, &endptr);
    DBG("eptr=%s, val=%g", endptr, *Val);
    if(endptr == string){
        WARNX("Double value not found");
        return 2;
    }
    return 0;
}

int main(int argc, char **argv){
    glob_pars *G = NULL; // default parameters see in cmdlnopts.c
    initial_setup();
    G = parse_args(argc, argv);
    TTY_descr *dev = new_tty(G->ttyname, G->speed, 64);
    if(!dev) return 1;
    size_t got, L = 0;
    char buff[BUFLEN], *ptr = buff;
    int errctr = 0;
    for(; errctr < ERRCTR_MAX; ++errctr){
        if(!tty_open(dev, 1)){
            sleep(1);
            continue;
        }
        while(read_tty(dev)); // clear buffer
        if(write_tty(dev->comfd, "?U\r\n", 3)){
            WARNX("write_tty()");
            continue;
        }
        double t0 = dtime();
        while(dtime() - t0 < 10.){ // timeout - 10s
            got = read_tty(dev);
            if(got == 0) continue;
            t0 = dtime();
            if(L + got > BUFLEN - 1) break;
            L += got;
            buff[L] = 0;
            if(BUFLEN > L){
                strncpy(ptr, dev->buf, dev->buflen);
                ptr += got;
            }else break;
            if(buff[L-1] == '\n' && L > 8) break; // full line received
        }
        if(L == 0){
            WARNX("Got nothing from TTY");
            continue;
        }else if(strncmp(buff, "<?U>", 4)){
            WARNX("Wrong answer: %s", buff);
            L = 0;
            ptr = buff;
            continue;
        }else break;
    }
    while(read_tty(dev));
    close_tty(&dev);
    if(errctr == ERRCTR_MAX){
        ERRX("No connection to meteostation");
    }
    ptr = &buff[4];
    for(size_t i = 4; i < L; ++i){
        char c = *ptr;
        if(isspace(c)) ++ptr;
    }
    char *eol = strchr(ptr, '\n');
    if(eol) *eol = 0;
    DBG("Now: %s\n", ptr);
    if(G->showraw) green("%s\n", ptr);
    double rain = 1., clouds = 1., temperature = -300., wind = 100.;//, windpeak = 100.;
    if(!getpar(ptr, &rain, "RT")) printf("Rain=%g\n", rain);
    if(!getpar(ptr, &clouds, "WU")) printf("Clouds=%g\n", clouds);
    if(!getpar(ptr, &temperature, "TE")) printf("Exttemp=%g\n", temperature);
    if(!getpar(ptr, &wind, "WG")){
        wind /= 3.6;
        printf("Wind=%.1f\n", wind);
    }
    /* if(!getpar(ptr, &windpeak, "WS")){
        windpeak /= 3.6;
        printf("Windpeak=%.1f\n", windpeak);
    }*/
    if(rain > 0.1 || clouds < 1900. || wind > 20.) return 1;
    return 0;
}
