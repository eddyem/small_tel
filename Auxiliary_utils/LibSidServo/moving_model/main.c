/*
 * This file is part of the moving_model project.
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

#include <stdio.h>
#include <usefull_macros.h>

#include "moving.h"

static movemodel_t *model = NULL;
static FILE *coordslog = NULL;
static double Tstart = 0.;

typedef struct{
    int help;
    char *ramptype;
    char *xlog;
    int dT;
} pars;

static pars G = {
    .ramptype = "d",
    .dT = 10000,
};

static limits_t limits = {
    .min = {.coord = -1e6, .speed = 0.1, .accel = 0.1},
    .max = {.coord = 1e6, .speed = 1e3, .accel = 50.},
    .jerk = 10.
};

static myoption opts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),          "show this help"},
    {"ramp",    NEED_ARG,   NULL,   'r',    arg_string, APTR(&G.ramptype),      "ramp type: \"d\", \"t\" or \"s\" - dumb, trapezoid, s-type"},
    {"deltat",  NEED_ARG,   NULL,   't',    arg_int,    APTR(&G.dT),            "time interval for monitoring (microseconds, >0)"},
    {"xlog",    NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.xlog),          "log file name for coordinates logging"},
    // TODO: add parameters for limits setting
    end_option
};

static int move(moveparam_t *tag){
    if(!tag) ERRX("move(): needs target");
    moveparam_t curpos;
    movestate_t curstate = model->get_state(&curpos);
    if(curstate == ST_MOVE) printf("Current state: moving; recalculte new parameters\n");
    if(!move_to(tag)){
        WARNX("move(): can't move to %g with max speed %g", tag->coord, tag->speed);
        return FALSE;
    }
    green("Moving from %g (speed=%g, acc=%g) to %g with maximal speed %g\n",
          curpos.coord, curpos.speed, curpos.accel, tag->coord, tag->speed);
    return TRUE;
}

// monitor moving with dump to file until T-Tnow == tnext or stop/error
// show position every dT
static void monit(double tnext){
    DBG("start monitoring");
    double t0 = nanot(), t = 0.;
    do{
        t = nanot();
        moveparam_t p;
        movestate_t st = model->get_state(&p);
        fprintf(coordslog, "%-9.4f\t%-10.4f\t%-10.4f\t%-10.4f\n",
            t - Tstart, p.coord, p.speed, p.accel);
        if(st == ST_STOP) break;
        usleep(G.dT);
    }while(nanot() - t0 < tnext);
    DBG("End of monitoring");
}

int main(int argc, char **argv){
    initial_setup();
    parseargs(&argc, &argv, opts);
    if(G.help) showhelp(-1, opts);
    if(G.xlog){
        coordslog = fopen(G.xlog, "w");
        if(!coordslog) ERR("Can't open %s", G.xlog);
    } else coordslog = stdout;
    if(G.dT < 1) G.dT = 1;
    fprintf(coordslog, "time  coordinate speed acceleration\n");
    ramptype_t ramp = RAMP_AMOUNT;
    if(*G.ramptype == 'd' || *G.ramptype == 'D') ramp = RAMP_DUMB;
    else if(*G.ramptype == 't' || *G.ramptype == 'T') ramp = RAMP_TRAPEZIUM;
    else if(*G.ramptype == 's' || *G.ramptype == 'S') ramp = RAMP_S;
    else ERRX("Point \"d\" (dumb), \"s\" (s-type), or \"t\" (trapez) for ramp type");
    model = init_moving(ramp, &limits);
    if(!model) ERRX("Can't init moving model: check parameters");
    Tstart = nanot();
    moveparam_t target = {.speed = 10., .coord = 20.};
    if(move(&target)) monit(0.5);
    for(int i = 0; i < 10; ++i){
        target.coord = -target.coord;
        if(move(&target)) monit(1.);
    }
    target.coord = 0.; target.speed = 20.;
    if(move(&target)) monit(100.);
    return 0;
    if(move(&target)) monit(1.);
    target.speed = 15.;
    target.coord = 30.;
    if(move(&target)) monit(1.);
    target.coord = 0.;
    if(move(&target)) monit(1.5);
    target.coord = 0.; target.speed = 20.;
    if(move(&target)) monit(1e6);
    usleep(5000);
    fclose(coordslog);
    return 0;
}
