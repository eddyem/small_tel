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

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <usefull_macros.h>

#include "dump.h"
#include "sidservo.h"
#include "simpleconv.h"

// swing telescope by given axis with given period and max amplitude, reqinterval=0.05 (min)

typedef struct{
    int help;
    int Ncycles;
    int Nswings;
    double period;
    double amplitude;
    char *coordsoutput;
    char *axis;
} parameters;

static parameters G = {
    .Ncycles = 20,
    .axis = "X",
    .Nswings = 10,
    .period = 1.,
    .amplitude = 5.,
};
static FILE *fcoords = NULL;

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"ncycles",     NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Ncycles),   "N cycles in stopped state (default: 20)"},
    {"coordsfile",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.coordsoutput),"output file with coordinates log"},
    {"axis",        NEED_ARG,   NULL,   'a',    arg_string, APTR(&G.axis),      "axis to move (X or Y)"},
    {"period",      NEED_ARG,   NULL,   'p',    arg_double, APTR(&G.period),    "swinging period (could be not reached if amplitude is too small) - not more than 900s (default: 1)"},
    {"amplitude",   NEED_ARG,   NULL,   'A',    arg_double, APTR(&G.amplitude), "max amplitude (could be not reaced if period is too small) - not more than 45deg (default: 5)"},
    {"nswings",     NEED_ARG,   NULL,   'N',    arg_int,    APTR(&G.Nswings),   "amount of swing periods (default: 10)"},
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
    .MountReqInterval = 0.05,
    .SepEncoder = 0
};

// dump thread
static void *dumping(void _U_ *u){
    dumpmoving(fcoords, 3600., G.Ncycles);
    return NULL;
}

// wait until mount is stopped within 5 cycles or until time reached t
void waithalf(double t){
    mountdata_t mdata;
    int ctr = -1;
    uint32_t millis = 0;
    double xlast = 0., ylast = 0.;
    while(ctr < 5){
        if(sl_dtime() >= t) return;
        usleep(1000);
        if(MCC_E_OK != Mount.getMountData(&mdata)){ WARNX("Can't get data"); continue;}
        if(mdata.millis == millis) continue;
        millis = mdata.millis;
        if(mdata.motposition.X != xlast || mdata.motposition.Y != ylast){
            DBG("NEQ: old=%g, now=%g", RAD2DEG(ylast), RAD2DEG(mdata.motposition.Y));
            xlast = mdata.motposition.X;
            ylast = mdata.motposition.Y;
            ctr = 0;
        }else{
            DBG("EQ: old=%g, now=%g", RAD2DEG(ylast), RAD2DEG(mdata.motposition.Y));
            ++ctr;
        }
    }
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
    if(G.Ncycles < 7) ERRX("Ncycles should be >7");
    if(G.amplitude < 0.01 || G.amplitude > 45.)
        ERRX("Amplitude should be from 0.01 to 45 degrees");
    if(G.period < 0.1 || G.period > 900.)
        ERRX("Period should be from 0.1 to 900s");
    if(G.Nswings < 1) ERRX("Nswings should be more than 0");
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
    pthread_t dthr;
    chk0(G.Ncycles);
    logmnt(fcoords, NULL);
    if(pthread_create(&dthr, NULL, dumping, NULL)) ERRX("Can't run dump thread");
    G.period /= 2.; // pause between commands
    double tagX, tagY;
    if(*G.axis == 'X'){
        tagX = DEG2RAD(G.amplitude); tagY = 0.;
    }else{
        tagX = 0.; tagY = DEG2RAD(G.amplitude);
    }
    double t = sl_dtime(), t0 = t;
    double divide = 2.;
    for(int i = 0; i < G.Nswings; ++i){
        Mount.moveTo(tagX, tagY);
        DBG("CMD: %g", sl_dtime()-t0);
        t += G.period / divide;
        divide = 1.;
        waithalf(t);
        DBG("Moved to +, t=%g", t-t0);
        DBG("CMD: %g", sl_dtime()-t0);
        Mount.moveTo(-tagX, -tagY);
        t += G.period;
        waithalf(t);
        DBG("Moved to -, t=%g", t-t0);
        DBG("CMD: %g", sl_dtime()-t0);
    }
    // be sure to move @ 0,0
    Mount.moveTo(0., 0.);
    // wait moving ends
    pthread_join(dthr, NULL);
#undef SCMD
    signals(0);
    return 0;
}
