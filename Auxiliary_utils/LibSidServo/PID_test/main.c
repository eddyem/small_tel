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

#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <usefull_macros.h>

#include "moving.h"
#include "PID.h"

// errors for states: slewing/pointing/guiding
// 10-degrees zone - Coordinate-driven PID
#define MAX_POINTING_ERR    (36000.)
// 1-arcminute zone - Velocity-dtiven PID
#define MAX_GUIDING_ERR     (60.)
// timeout to "forget" old data from I sum array; seconds
#define PID_I_PERIOD        (3.)

// PID for coordinate-driven and velocity-driven parts
static PIDController_t *pidC = NULL, *pidV = NULL;
static movemodel_t *model = NULL;
static FILE *coordslog = NULL;

typedef enum{
    Slewing,
    Pointing,
    Guiding
} state_t;

static state_t state = Slewing;

typedef struct{
    int help;
    char *ramptype;
    char *xlog;
    double dTmon;
    double dTcorr;
    double Tend;
    double minerr;
    double startcoord;
    double error;
    PIDpar_t gainC, gainV;
} pars;

static pars G = {
    .dTmon = 0.01,
    .dTcorr = 0.05,
    .Tend = 100.,
    .minerr = 0.1,
    .gainC.P = 0.1,
    .gainV.P = 0.1,
    .startcoord = 100.,
};

static limits_t limits = {
    .min = {.coord = -1e6, .speed = 0.01, .accel = 0.1},
    .max = {.coord = 6648000, .speed = 36000., .accel = 36000.}
};


static sl_option_t opts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),          "show this help"},
    {"tmon",    NEED_ARG,   NULL,   'T',    arg_double, APTR(&G.dTmon),         "time interval for monitoring (seconds, default: 0.001)"},
    {"tcor",    NEED_ARG,   NULL,   't',    arg_double, APTR(&G.dTcorr),        "time interval for corrections (seconds, default: 0.05)"},
    {"xlog",    NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.xlog),          "log file name for coordinates logging"},
    {"tend",    NEED_ARG,   NULL,   'e',    arg_double, APTR(&G.Tend),          "end time of monitoring (seconds, default: 100)"},
    {"minerr",  NEED_ARG,   NULL,   'm',    arg_double, APTR(&G.minerr),        "minimal error for corrections (units, default: 0.1)"},
    {"propC",   NEED_ARG,   NULL,   'P',    arg_double, APTR(&G.gainC.P),       "P-coefficient of coordinate-driven PID"},
    {"integC",  NEED_ARG,   NULL,   'I',    arg_double, APTR(&G.gainC.I),       "I-coefficient of coordinate-driven PID"},
    {"diffC",   NEED_ARG,   NULL,   'D',    arg_double, APTR(&G.gainC.D),       "D-coefficient of coordinate-driven PID"},
    {"propV",   NEED_ARG,   NULL,   'p',    arg_double, APTR(&G.gainV.P),       "P-coefficient of velocity-driven PID"},
    {"integV",  NEED_ARG,   NULL,   'i',    arg_double, APTR(&G.gainV.I),       "I-coefficient of velocity-driven PID"},
    {"diffV",   NEED_ARG,   NULL,   'd',    arg_double, APTR(&G.gainV.D),       "D-coefficient of velocity-driven PID"},
    {"xstart",  NEED_ARG,   NULL,   '0',    arg_double, APTR(&G.startcoord),    "starting coordinate of target"},
    {"error",   NEED_ARG,   NULL,   'E',    arg_double, APTR(&G.error),         "error range"},
    // TODO: add parameters for limits setting
    end_option
};

// calculate coordinate target for given time (starting from zero)
static double target_coord(double t){
    if(t > 20. && t < 30.) return 0.;
    //double pos = G.startcoord + 15. * t + G.error * (drand48() - 0.5);
    double pos = G.startcoord + 15. * sin(2*M_PI * t / 10.) + G.error * (drand48() - 0.5);
    return pos;
}

static double getNewSpeed(const moveparam_t *p, double targcoord, double dt){
    double error = targcoord - p->coord, fe = fabs(error);
    PIDController_t *pid = NULL;
    switch(state){
        case Slewing:
            if(fe < MAX_POINTING_ERR){
                pid_clear(pidC);
                state = Pointing;
                green("--> Pointing\n");
                pid = pidC;
            }else{
                red("Slewing...\n");
                return (error > 0.) ? limits.max.speed : -limits.max.speed;
            }
            break;
        case Pointing:
            if(fe < MAX_GUIDING_ERR){
                pid_clear(pidV);
                state = Guiding;
                green("--> Guiding\n");
                pid = pidV;
            }else if(fe > MAX_POINTING_ERR){
                red("--> Slewing\n");
                state = Slewing;
                return (error > 0.) ? limits.max.speed : -limits.max.speed;
            } else pid = pidC;
            break;
        case Guiding:
            pid= pidV;
            if(fe > MAX_GUIDING_ERR){
                red("--> Pointing\n");
                state = Pointing;
                pid_clear(pidC);
                pid = pidC;
            }else if(fe < G.minerr){
                    green("At target\n");
            }else printf("Current error: %g\n", fe);
            break;
    }
    if(!pid){
        WARNX("where is PID?"); return p->speed;
    }
    double tagspeed = pid_calculate(pid, error, dt);
    if(state == Guiding) return p->speed + tagspeed;
    return tagspeed;
}
// -P0.8 -D0.1 -I0.02 -p20 -d.5 -i.02
// another: P0.8 -D0.1 -I0.02 -p5 -d0.9 -i0.1

static void start_model(double Tend){
    double T = 0., Tcorr = 0.;
    moveparam_t target;
    uint64_t N = 0;
    double errmax = 0., errsum = 0., errsum2 = 0.;
    while(T <= Tend){
        moveparam_t p;
        movestate_t st = model->get_state(&p);
        if(st == ST_MOVE) st = model->proc_move(&p, T);
        double nextcoord = target_coord(T);
        double error = nextcoord - p.coord;
        if(state == Guiding){
            double ae = fabs(error);
            if(ae > errmax) errmax = ae;
            errsum += error; errsum2 += error * error;
            ++N;
        }
        if(T - Tcorr >= G.dTcorr){ // check correction
            double speed = getNewSpeed(&p, nextcoord, T - Tcorr);
            target.coord = (speed > 0) ? p.coord + 5e5 : p.coord - 5e5;
            target.speed = fabs(speed);
            double res_speed = limits.max.speed / 2.;
            if(target.speed > limits.max.speed){
                target.speed = limits.max.speed;
                res_speed = limits.max.speed / 4.;
            }else if(target.speed < limits.min.speed){
                target.speed = limits.min.speed;
                res_speed = limits.min.speed * 4.;
            }
            if(!move_to(&target, T)){
                target.speed = res_speed;
                if(!move_to(&target, T))
                    WARNX("move(): can't move to %g with max speed %g", target.coord, target.speed);
            }
            DBG("%g: tag/cur speed= %g / %g; tag/cur pos = %g / %g; err = %g", T, target.speed, p.speed, target.coord, p.coord, error);
            Tcorr = T;
        }
        // make log
        fprintf(coordslog, "%-9.4f\t%-10.4f\t%-10.4f\t%-10.4f\t%-10.4f\t%-10.4f\n",
                T, nextcoord, p.coord, p.speed, p.accel, error);
        T += G.dTmon;
    }
    printf("\n\n\n"); red("Calculated errors in `guiding` mode:\n");
    double mean = errsum / (double)N;
    printf("max error: %g, mean error: %g, std: %g\n\n", errmax, mean, sqrt(errsum2/(double)N - mean*mean));
}

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, opts);
    if(G.help) sl_showhelp(-1, opts);
    if(G.xlog){
        coordslog = fopen(G.xlog, "w");
        if(!coordslog) ERR("Can't open %s", G.xlog);
    } else coordslog = stdout;
    if(G.dTmon <= 0.) ERRX("tmon should be > 0.");
    if(G.dTcorr <= 0. || G.dTcorr > 1.) ERRX("tcor should be > 0. and < 1.");
    if(G.Tend <= 0.) ERRX("tend should be > 0.");
    pidC = pid_create(&G.gainC, PID_I_PERIOD / G.dTcorr);
    pidV = pid_create(&G.gainV, PID_I_PERIOD / G.dTcorr);
    if(!pidC || !pidV) ERRX("Can't init PID regulators");
    model = init_moving(&limits);
    if(!model) ERRX("Can't init moving model: check parameters");
    fprintf(coordslog, "%-9s\t%-10s\t%-10s\t%-10s\t%-10s\t%-10s\n", "time", "target", "curpos", "speed", "accel", "error");
    start_model(G.Tend);
    pid_delete(&pidC);
    pid_delete(&pidV);
    fclose(coordslog);
    return 0;
}
