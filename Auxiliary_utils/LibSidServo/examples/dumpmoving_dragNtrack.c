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

// move telescope to target using short command and force it to track mode

#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <usefull_macros.h>

#include "conf.h"
#include "dump.h"
#include "sidservo.h"
#include "simpleconv.h"

typedef struct{
    int help;
    int Ncycles;
    int relative;
    double reqint;
    char *coordsoutput;
    char *conffile;
    char *axis;
} parameters;

static parameters G = {
    .Ncycles = 40,
    .reqint = -1.,
    .axis = "X",
};

static FILE *fcoords = NULL;

static coords_t M;

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"ncycles",     NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Ncycles),   "N cycles in stopped state (default: 40)"},
    {"coordsfile",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.coordsoutput),"output file with coordinates log"},
    {"reqinterval", NEED_ARG,   NULL,   'i',    arg_double, APTR(&G.reqint),    "mount requests interval (default: 0.1)"},
    {"axis",        NEED_ARG,   NULL,   'a',    arg_string, APTR(&G.axis),      "axis to move (X, Y or B for both)"},
    {"conffile",    NEED_ARG,   NULL,   'C',    arg_string, APTR(&G.conffile),  "configuration file name"},
    {"relative",    NO_ARGS,    NULL,   'r',    arg_int,    APTR(&G.relative),  "relative move"},
    end_option
};

void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    Mount.quit();
    exit(sig);
}

// dump thread
static void *dumping(void _U_ *u){
    dumpmoving(fcoords, 3600., G.Ncycles);
    return NULL;
}

// return TRUE if motor position is reached +- 0.01 degrees
#define XYcount (DEG2RAD(0.01))
static int Wait(double tag){
    mountdata_t mdata;
    red("Wait for %g degrees\n", RAD2DEG(tag));
    int errcnt = 0;
    double sign = 0.;
    uint32_t millis = 0;
    double curpos = 0.;
    do{
        if(MCC_E_OK != Mount.getMountData(&mdata)) ++errcnt;
        else{
            errcnt = 0;
            if(mdata.millis == millis) continue;
            millis = mdata.millis;
            if(*G.axis == 'X') curpos = mdata.motposition.X;
            else curpos = mdata.motposition.Y;
            if(sign == 0.) sign = (curpos > tag) ? 1. : -1.;
            //printf("%s=%g deg, need %g deg; delta=%g arcmin\n", G.axis, RAD2DEG(curpos),
            //       RAD2DEG(tag), RAD2DEG(sign*(curpos - tag))*60.);
        }
    }while(sign*(curpos - tag) > XYcount && errcnt < 10);
    if(errcnt >= 10){
        WARNX("Too much errors");
        return FALSE;
    }
    green("%s reached position %g degrees\n", G.axis, RAD2DEG(tag));
    fflush(stdout);
    return TRUE;
}

// move X/Y to 40 degr with given speed until given coord
static void move(double target, double limit, double speed){
    green("Move %s to %g until %g with %gdeg/s\n", G.axis, target, limit, speed);
    short_command_t cmd = {0};
    if(*G.axis == 'X' || *G.axis == 'B'){
        cmd.Xmot = DEG2RAD(target) + M.X;
        cmd.Xspeed = DEG2RAD(speed);
        limit = DEG2RAD(limit) + M.X;
    }
    if(*G.axis == 'Y' || *G.axis == 'B'){
        cmd.Ymot = DEG2RAD(target) + M.Y;
        cmd.Yspeed = DEG2RAD(speed);
        if(*G.axis != 'B') limit = DEG2RAD(limit) + M.Y;
    }
    if(MCC_E_OK != Mount.shortCmd(&cmd)) ERRX("Can't run command");
    if(!Wait(limit)) signals(9);
}


int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    if(strcmp(G.axis, "X") && strcmp(G.axis, "Y") && strcmp(G.axis, "B")){
        WARNX("\"Axis\" should be X, Y or B");
        return 1;
    }
    if(G.coordsoutput){
        if(!(fcoords = fopen(G.coordsoutput, "w")))
            ERRX("Can't open %s", G.coordsoutput);
    }else fcoords = stdout;
    conf_t *Config = readServoConf(G.conffile);
    if(!Config){
        dumpConf();
        return 1;
    }
    if(G.reqint > 0.) Config->MountReqInterval = G.reqint;
    if(MCC_E_OK != Mount.init(Config)){
        WARNX("Can't init devices");
        return 1;
    }
    if(!getPos(&M, NULL)) ERRX("Can't get current position");
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    // move to X=40 degr with different speeds
    pthread_t dthr;
    logmnt(fcoords, NULL);
    if(pthread_create(&dthr, NULL, dumping, NULL)) ERRX("Can't run dump thread");
    // goto 10 degr with 2d/s and try to track for 8 seconds
    move(10., 10.+2./60., 2.);
    // be sure to move @ 0,0
    Mount.moveTo(&M.X, &M.Y);
    // wait moving ends
    pthread_join(dthr, NULL);
    signals(0);
    return 0;
}
