/*
 * This file is part of the libsidservo project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

// move telescope to given MOTOR position in degrees

#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <usefull_macros.h>

#include "conf.h"
#include "dump.h"
#include "sidservo.h"
#include "simpleconv.h"

typedef struct{
    int help;
    int Ncycles;
    int wait;
    int relative;
    char *coordsoutput;
    char *conffile;
    double X;
    double Y;
} parameters;

static parameters G = {
    .Ncycles = 40,
    .X = NAN,
    .Y = NAN,
};

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"ncycles",     NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Ncycles),   "N cycles of waiting in stopped state (default: 40)"},
    {"newx",        NEED_ARG,   NULL,   'X',    arg_double, APTR(&G.X),         "new X coordinate"},
    {"newy",        NEED_ARG,   NULL,   'Y',    arg_double, APTR(&G.Y),         "new Y coordinate"},
    {"output",      NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.coordsoutput),"file to log coordinates"},
    {"wait",        NO_ARGS,    NULL,   'w',    arg_int,    APTR(&G.wait),      "wait until mowing stopped"},
    {"relative",    NO_ARGS,    NULL,   'r',    arg_int,    APTR(&G.relative),  "relative move"},
    {"conffile",    NEED_ARG,   NULL,   'C',    arg_string, APTR(&G.conffile),  "configuration file name"},
    end_option
};

static FILE* fcoords = NULL;
static pthread_t dthr;

void signals(int sig){
    pthread_cancel(dthr);
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    Mount.quit();
    if(fcoords) fclose(fcoords);
    exit(sig);
}

// dump thread
static void *dumping(void _U_ *u){
    dumpmoving(fcoords, 3600., G.Ncycles);
    return NULL;
}

int main(int _U_ argc, char _U_ **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help)
        sl_showhelp(-1, cmdlnopts);
    conf_t *Config = readServoConf(G.conffile);
    if(!Config){
        dumpConf();
        return 1;
    }
    if(MCC_E_OK != Mount.init(Config)) ERRX("Can't init mount");
    coords_t M;
    if(!getPos(&M, NULL)) ERRX("Can't get current position");
    if(G.coordsoutput){
        if(!G.wait) green("When logging I should wait until moving ends; added '-w'");
        G.wait = 1;
    }
    if(G.coordsoutput){
        if(!(fcoords = fopen(G.coordsoutput, "w")))
            ERRX("Can't open %s", G.coordsoutput);
        logmnt(fcoords, NULL);
        if(pthread_create(&dthr, NULL, dumping, NULL)) ERRX("Can't run dump thread");
    }
    printf("Mount position: X=%g, Y=%g\n", RAD2DEG(M.X), RAD2DEG(M.Y));
    if(isnan(G.X) && isnan(G.Y)) goto out;
    double *xtag = NULL, *ytag = NULL, xr, yr;
    if(!isnan(G.X)){
        xr = DEG2RAD(G.X);
        if(G.relative) xr += M.X;
        xtag = &xr;
    }
    if(!isnan(G.Y)){
        yr = DEG2RAD(G.Y);
        if(G.relative) yr += M.Y;
        ytag = &yr;
    }
    printf("Moving to ");
    if(xtag) printf("X=%gdeg ", G.X);
    if(ytag) printf("Y=%gdeg", G.Y);
    printf("\n");
    Mount.moveTo(xtag, ytag);
    if(G.wait){
        sleep(1);
        waitmoving(G.Ncycles);
        if(!getPos(&M, NULL)) WARNX("Can't get current position");
        else printf("New mount position: X=%g, Y=%g\n", RAD2DEG(M.X), RAD2DEG(M.Y));
    }
out:
    if(G.coordsoutput) pthread_join(dthr, NULL);
    if(G.wait){
        if(getPos(&M, NULL)) printf("Mount position: X=%g, Y=%g\n", RAD2DEG(M.X), RAD2DEG(M.Y));
        Mount.quit();
    }
    return 0;
}
