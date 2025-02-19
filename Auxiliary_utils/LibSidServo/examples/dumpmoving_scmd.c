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

// dump telescope moving using short binary commands

#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <usefull_macros.h>

#include "dump.h"
#include "sidservo.h"
#include "simpleconv.h"

typedef struct{
    int help;
    int Ncycles;
    double reqint;
    char *coordsoutput;
    char *axis;
} parameters;

static parameters G = {
    .Ncycles = 40,
    .reqint = 0.1,
    .axis = "X",
};
static FILE *fcoords = NULL;

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"ncycles",     NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Ncycles),   "N cycles in stopped state (default: 40)"},
    {"coordsfile",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.coordsoutput),"output file with coordinates log"},
    {"reqinterval", NEED_ARG,   NULL,   'i',    arg_double, APTR(&G.reqint),    "mount requests interval (default: 0.1)"},
    {"axis",        NEED_ARG,   NULL,   'a',    arg_string, APTR(&G.axis),      "axis to move (X or Y)"},
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

static conf_t Config = {
    .MountDevPath = "/dev/ttyUSB0",
    .MountDevSpeed = 19200,
    //.EncoderDevPath = "/dev/ttyUSB1",
    //.EncoderDevSpeed = 153000,
    .MountReqInterval = 0.1,
    .SepEncoder = 0
};

// dump thread
static void *dumping(void _U_ *u){
    dumpmoving(fcoords, 3600., G.Ncycles);
    return NULL;
}

// return TRUE if motor position is reached +- 0.1 degrees
#define XYcount (DEG2RAD(0.1))
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
    return TRUE;
}

// move X to 40 degr with given speed until given coord
static void move(double target, double limit, double speed){
#define SCMD()   do{if(MCC_E_OK != Mount.shortCmd(&cmd)) ERRX("Can't run command"); }while(0)
    green("Move %s to %g until %g with %gdeg/s\n", G.axis, target, limit, speed);
    short_command_t cmd = {0};
    if(*G.axis == 'X'){
        cmd.Xmot = DEG2RAD(target);
        cmd.Xspeed = DEG2RAD(speed);
    }else{
        cmd.Ymot = DEG2RAD(target);
        cmd.Yspeed = DEG2RAD(speed);
    }
    SCMD();
    if(!Wait(DEG2RAD(limit))) signals(9);
#undef SCMD
}


int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    if(strcmp(G.axis, "X") && strcmp(G.axis, "Y")){
        WARNX("\"Axis\" should be X or Y");
        return 1;
    }
    if(G.coordsoutput){
        if(!(fcoords = fopen(G.coordsoutput, "w")))
            ERRX("Can't open %s", G.coordsoutput);
    }else fcoords = stdout;
    Config.MountReqInterval = G.reqint;
    mcc_errcodes_t e = Mount.init(&Config);
    if(e != MCC_E_OK){
        WARNX("Can't init devices");
        return 1;
    }
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    // move to X=40 degr with different speeds
    pthread_t dthr;
    chk0(G.Ncycles);
    logmnt(fcoords, NULL);
    if(pthread_create(&dthr, NULL, dumping, NULL)) ERRX("Can't run dump thread");
    // goto 1 degr with 1'/s
    move(10., 1., 1./60.);
    // goto 2 degr with 2'/s
    move(10., 2., 2./60.);
    // goto 3 degr with 5'/s
    move(10., 3., 5./60.);
    // goto 4 degr with 10'/s
    move(10., 4., 10./60.);
    // and go back with 5deg/s
    move(0., 0., 5.);
    // be sure to move @ 0,0
    Mount.moveTo(0., 0.);
    // wait moving ends
    pthread_join(dthr, NULL);
#undef SCMD
    signals(0);
    return 0;
}
