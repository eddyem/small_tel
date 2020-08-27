/*
 * This file is part of the ttyterm project.
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
    if(!dev || !(dev = tty_open(dev, 1))) return 1; // open exclusively
    while(read_tty(dev)); // clear buffer
    if(write_tty(dev->comfd, "?U\r\n", 3)) ERR("write_tty()");
    size_t got, L = 0;
    char buff[BUFLEN], *ptr = buff;
    double t0 = dtime();
    while(dtime() - t0 < 1.){ // timeout - 1s
        got = read_tty(dev);
        if(got == 0) continue;
        t0 = dtime();
        L += got;
        if(BUFLEN > L){
            strncpy(ptr, dev->buf, dev->buflen);
            ptr += got;
        }else break;
    }
    buff[L] = 0;
    if(L == 0) ERRX("Got nothing from TTY");
    if(strncmp(buff, "<?U>", 4)) ERRX("Wrong answer: %s", buff);
    ptr = &buff[4];
    for(size_t i = 4; i < L; ++i){
        char c = *ptr;
        if(isspace(c)) ++ptr;
    }
    char *eol = strchr(ptr, '\n');
    if(eol) *eol = 0;
    DBG("Now: %s\n", ptr);
    double rain = 1., clouds = 1.;
    if(!getpar(ptr, &rain, "RT")) printf("Rain=%g\n", rain);
    if(!getpar(ptr, &clouds, "WK")) printf("Clouds=%g\n", clouds);
    close_tty(&dev);
    if(rain > 0.1) return 1;
    return 0;
}
