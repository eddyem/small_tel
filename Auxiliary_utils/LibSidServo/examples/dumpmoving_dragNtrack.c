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
// also do some corrections while moving

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

// Original XXI=6827
// XXD=136546
// XXB=4915666

typedef struct{
    int help;
    int Ncycles;
    double reqint;
    char *coordsoutput;
    char *conffile;
} parameters;

static parameters G = {
    .Ncycles = 40,
    .reqint = -1.,
};

static FILE *fcoords = NULL;

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"ncycles",     NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Ncycles),   "N cycles in stopped state (default: 40)"},
    {"coordsfile",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.coordsoutput),"output file with coordinates log"},
    {"reqinterval", NEED_ARG,   NULL,   'i',    arg_double, APTR(&G.reqint),    "mount requests interval (default: 0.1)"},
    {"conffile",    NEED_ARG,   NULL,   'C',    arg_string, APTR(&G.conffile),  "configuration file name"},
    end_option
};

static mcc_errcodes_t return2zero();

void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    return2zero();
    sleep(5);
    Mount.quit();
    exit(sig);
}

// dump thread
static void *dumping(void _U_ *u){
    dumpmoving(fcoords, 3600., G.Ncycles);
    return NULL;
}

// return TRUE if motor position is reached +- 0.01 degrees
#define XYcount (DEG2RAD(0.3))
// tag in degrees!
static int Wait(double tag, int isX){
    mountdata_t mdata;
    red("Wait for %g degrees\n", tag);
    tag = DEG2RAD(tag);
    int errcnt = 0;
    uint32_t millis = 0;
    double curpos = 0.;
    double t0 = sl_dtime();
    do{
        if(MCC_E_OK != Mount.getMountData(&mdata)) ++errcnt;
        else{
            errcnt = 0;
            if(mdata.millis == millis) continue;
            millis = mdata.millis;
            if(isX) curpos = mdata.motXposition.val;
            else curpos = mdata.motYposition.val;
        }
        double t = sl_dtime();
        if(t - t0 > 1.){
            t0 = t;
            printf("\t\tCurrent MOT X/Y: %g / %g deg\n", RAD2DEG(mdata.motXposition.val),
                    RAD2DEG(mdata.motYposition.val));
        }
    }while(fabs(curpos - tag) > XYcount && errcnt < 10);
    if(errcnt >= 10){
        WARNX("Too much errors");
        return FALSE;
    }
    green("%s reached position %g degrees\n", (isX) ? "X" : "Y", RAD2DEG(tag));
    fflush(stdout);
    return TRUE;
}

// previous GOTO coords/speeds for `mkcorr`
static coordpair_t lastTag = {0}, lastSpeed = {0};

// slew to given position and start tracking
// pos/speed in deg and deg/s
static mcc_errcodes_t gotos(const coordpair_t *target, const coordpair_t *speed){
    short_command_t cmd = {0};
    DBG("Try to move to (%g, %g) with speed (%g, %g)",
        target->X, target->Y, speed->X, speed->Y);
    cmd.Xmot = DEG2RAD(target->X); cmd.Ymot = DEG2RAD(target->Y);
    cmd.Xspeed = DEG2RAD(speed->X);
    cmd.Yspeed = DEG2RAD(speed->Y);
    lastTag = *target;
    lastSpeed = *speed;
    /*cmd.xychange = 1;
    cmd.XBits = 108;
    cmd.YBits = 28;*/
    return Mount.shortCmd(&cmd);
}

static mcc_errcodes_t return2zero(){
    short_command_t cmd = {0};
    DBG("Try to move to zero");
    cmd.Xmot = 0.; cmd.Ymot = 0.;
    cmd.Xspeed = MCC_MAX_X_SPEED;
    cmd.Yspeed = MCC_MAX_Y_SPEED;
    /*cmd.xychange = 1;
    cmd.XBits = 100;
    cmd.YBits = 20;*/
    return Mount.shortCmd(&cmd);
}

static mcc_errcodes_t mkcorr(coordpair_t *adder, coordpair_t *time){
    long_command_t cmd = {0};
    cmd.Xspeed = DEG2RAD(lastSpeed.X);
    cmd.Yspeed = DEG2RAD(lastSpeed.Y);
    cmd.Xmot = DEG2RAD(lastTag.X);
    cmd.Ymot = DEG2RAD(lastTag.Y);
    cmd.Xadder = DEG2RAD(adder->X); cmd.Yadder = DEG2RAD(adder->Y);
    cmd.Xatime = time->X; cmd.Yatime = time->Y;
    return Mount.longCmd(&cmd);
}

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
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
    //if(!getPos(&M, NULL)) ERRX("Can't get current position");
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    // move to X=40 degr with different speeds
    pthread_t dthr;
    logmnt(fcoords, NULL);
    if(pthread_create(&dthr, NULL, dumping, NULL)) ERRX("Can't run dump thread");
    // move to 10/10
    coordpair_t coords, speeds, adders, tadd;
    coords = (coordpair_t){.X = 10., .Y = 20.};
    speeds = (coordpair_t){.X = 1., .Y = 2.};
    adders = (coordpair_t){.X = 0.01, .Y = 0.01};
    tadd = (coordpair_t){.X = 1., .Y = 2.};
    green("Goto\n");
    if(MCC_E_OK != gotos(&coords, &speeds)) ERRX("Can't go");
    DBG("c/s: %g %g %g %g", coords.X, coords.Y, speeds.X, speeds.Y);
    green("Waiting X==4\n");
    Wait(4., 1);
    // now we are at point by Y but still moving by X; make small correction by X/Y into '+'
    green("Mkcorr 1\n");
    if(MCC_E_OK != mkcorr(&adders, &tadd)) ERRX("Can't make corr");
    green("Waiting X==6\n");
    Wait(6., 1);
    green("Goto more\n");
    coords = (coordpair_t){.X = 20., .Y = 30.};
    if(MCC_E_OK != gotos(&coords, &speeds)) ERRX("Can't go");
    DBG("c/s: %g %g %g %g", coords.X, coords.Y, speeds.X, speeds.Y);
    green("Waiting Y==14\n");
    Wait(14., 0);
    // now we are @ point, make the same small correction again
    green("Mkcorr 2\n");
    if(MCC_E_OK != mkcorr(&coords, &speeds)) ERRX("Can't make corr");
    // wait for 5 seconds
    green("Wait for 5 seconds\n");
    sleep(5);
    // return to zero and wait
    green("Return 2 zero and wait\n");
    if(MCC_E_OK != return2zero()) ERRX("Can't return");
    Wait(0., 0);
    Wait(0., 1);
    // wait moving ends
    pthread_join(dthr, NULL);
    signals(0);
    return 0;
}
