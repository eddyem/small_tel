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
    .Ncycles = 10,
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
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    DBG("Quit");
    Mount.quit();
    DBG("close");
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
    coordval_pair_t M, E;
    if(!getPos(&M, &E)) ERRX("Can't get current position");
    printf("Current time: %.10f\n", Mount.timeFromStart());
    if(G.coordsoutput){
        if(!G.wait) green("When logging I should wait until moving ends; added '-w'\n");
        G.wait = 1;
        if(!(fcoords = fopen(G.coordsoutput, "w")))
            ERRX("Can't open %s", G.coordsoutput);
        logmnt(fcoords, NULL);
        if(pthread_create(&dthr, NULL, dumping, NULL)) ERRX("Can't run dump thread");
    }
    M.X.val = RAD2DEG(M.X.val);
    M.Y.val = RAD2DEG(M.Y.val);
    printf("Mount position: X=%g, Y=%g; encoders: X=%g, Y=%g\n", M.X.val, M.Y.val,
           RAD2DEG(E.X.val), RAD2DEG(E.Y.val));
    if(isnan(G.X) && isnan(G.Y)) goto out;
    coordpair_t tag;
    if(isnan(G.X)){
        if(G.relative) G.X = 0.;
        else G.X = M.X.val;
    }
    if(isnan(G.Y)){
        if(G.relative) G.Y = 0.;
        else G.Y = M.Y.val;
    }
    if(G.relative){
        G.X += M.X.val;
        G.Y += M.Y.val;
    }
    printf("Moving to X=%gdeg, Y=%gdeg\n", G.X, G.Y);
    tag.X = DEG2RAD(G.X); tag.Y = DEG2RAD(G.Y);
    Mount.moveTo(&tag);
    if(G.wait){
        sleep(1);
        waitmoving(G.Ncycles);
        if(!getPos(&M, NULL)) WARNX("Can't get current position");
        else printf("New mount position: X=%g, Y=%g\n", RAD2DEG(M.X.val), RAD2DEG(M.Y.val));
    }
out:
    DBG("JOIN");
    if(G.coordsoutput) pthread_join(dthr, NULL);
    DBG("QUIT");
    if(G.wait){
        if(getPos(&M, NULL)) printf("Mount position: X=%g, Y=%g\n", RAD2DEG(M.X.val), RAD2DEG(M.Y.val));
        Mount.quit();
    }
    return 0;
}
