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

// errors for states: slewing/pointing/guiding
#define MAX_POINTING_ERR    (50.)
#define MAX_GUIDING_ERR     (5.)
// timeout to "forget" old data from I sum array; seconds
#define PID_I_PERIOD        (3.)

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
    double P, I, D;
} pars;

static pars G = {
    .ramptype = "t",
    .dTmon = 0.01,
    .dTcorr = 0.05,
    .Tend = 100.,
    .minerr = 0.1,
    .P = 0.8,
};

static limits_t limits = {
    .min = {.coord = -1e6, .speed = 0.01, .accel = 0.1},
    .max = {.coord = 1e6, .speed = 1e3, .accel = 500.},
    .jerk = 10.
};

typedef struct {
    double kp, ki, kd;  // PID gains
    double prev_error;  // Previous error
    double integral;    // Integral term
    double *pidIarray;  // array for Integral
    size_t pidIarrSize; // it's size
    size_t curIidx;     // and index of current element
} PIDController;

static PIDController pid;

static sl_option_t opts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),          "show this help"},
    {"ramp",    NEED_ARG,   NULL,   'r',    arg_string, APTR(&G.ramptype),      "ramp type: \"d\", \"t\" or \"s\" - dumb, trapezoid, s-type"},
    {"tmon",    NEED_ARG,   NULL,   'T',    arg_double, APTR(&G.dTmon),         "time interval for monitoring (seconds, default: 0.001)"},
    {"tcor",    NEED_ARG,   NULL,   't',    arg_double, APTR(&G.dTcorr),        "time interval for corrections (seconds, default: 0.05)"},
    {"xlog",    NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.xlog),          "log file name for coordinates logging"},
    {"tend",    NEED_ARG,   NULL,   'e',    arg_double, APTR(&G.Tend),          "end time of monitoring (seconds, default: 100)"},
    {"minerr",  NEED_ARG,   NULL,   'm',    arg_double, APTR(&G.minerr),        "minimal error for corrections (units, default: 0.1)"},
    {"prop",    NEED_ARG,   NULL,   'P',    arg_double, APTR(&G.P),             "P-coefficient of PID"},
    {"integ",   NEED_ARG,   NULL,   'I',    arg_double, APTR(&G.I),             "I-coefficient of PID"},
    {"diff",    NEED_ARG,   NULL,   'D',    arg_double, APTR(&G.D),             "D-coefficient of PID"},
    // TODO: add parameters for limits setting
    end_option
};

// calculate coordinate target for given time (starting from zero)
static double target_coord(double t){
    if(t > 20. && t < 30.) return target_coord(20.);
    double pos = 150. + 10. * sin(M_2_PI * t / 10.) + 0.02 * (drand48() - 0.5);
    return pos;
}

/* P-only == oscillations
static double getNewSpeed(const moveparam_t *p, double targcoord, double dt){
    double error = targcoord - p->coord;
    if(fabs(error) < G.minerr) return p->speed;
    return p->speed + error / dt / 500.;
}
*/

static void pid_init(PIDController *pid, double kp, double ki, double kd) {
    pid->kp = fabs(kp);
    pid->ki = fabs(ki);
    pid->kd = fabs(kd);
    pid->prev_error = 0.;
    pid->integral = 0.;
    pid->curIidx = 0;
    pid->pidIarrSize = PID_I_PERIOD / G.dTcorr;
    if(pid->pidIarrSize < 2) ERRX("I-array for PID have less than 2 elements");
    pid->pidIarray = MALLOC(double, pid->pidIarrSize);
}

static void pid_clear(PIDController *pid){
    if(!pid) return;
    bzero(pid->pidIarray, sizeof(double) * pid->pidIarrSize);
    pid->integral = 0.;
    pid->prev_error = 0.;
    pid->curIidx = 0;
}

static double getNewSpeed(const moveparam_t *p, double targcoord, double dt){
    double error = targcoord - p->coord, fe = fabs(error);
    switch(state){
        case Slewing:
            if(fe < MAX_POINTING_ERR){
                pid_clear(&pid);
                state = Pointing;
                green("--> Pointing\n");
            }else{
                red("Slewing...\n");
                return (error > 0.) ? limits.max.speed : -limits.max.speed;
            }
            break;
        case Pointing:
            if(fe < MAX_GUIDING_ERR){
                pid_clear(&pid);
                state = Guiding;
                green("--> Guiding\n");
            }else if(fe > MAX_POINTING_ERR){
                red("--> Slewing\n");
                state = Slewing;
                return (error > 0.) ? limits.max.speed : -limits.max.speed;
            }
            break;
        case Guiding:
            if(fe > MAX_GUIDING_ERR){
                red("--> Pointing\n");
                state = Pointing;
            }else if(fe < G.minerr){
                    green("At target\n");
                    //pid_clear(&pid);
                    //return p->speed;
            }
            break;
    }

    red("Calculate PID\n");
    double oldi = pid.pidIarray[pid.curIidx], newi = error * dt;
    pid.pidIarray[pid.curIidx++] = oldi;
    if(pid.curIidx >= pid.pidIarrSize) pid.curIidx = 0;
    pid.integral += newi - oldi;
    double derivative = (error - pid.prev_error) / dt;
    pid.prev_error = error;
    DBG("P=%g, I=%g, D=%g", pid.kp * error, pid.integral, derivative);
    double add = (pid.kp * error + pid.ki * pid.integral + pid.kd * derivative);
    if(state == Pointing) add /= 3.;
    else if(state == Guiding) add /= 7.;
    DBG("ADD = %g; new speed = %g", add, p->speed + add);
    if(state == Guiding) return p->speed + add  / dt / 10.;
    return add / dt;
}
// ./moving  -l coords -P.5 -I.05 -D1.5
// ./moving  -l coords -P1.3 -D1.6

static void start_model(double Tend){
    double T = 0., Tcorr = 0.;//, Tlast = 0.;
    moveparam_t target;
    while(T <= Tend){
        moveparam_t p;
        movestate_t st = model->get_state(&p);
        if(st == ST_MOVE) st = model->proc_move(&p, T);
        double nextcoord = target_coord(T);
        double error = nextcoord - p.coord;
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
    pid_init(&pid, G.P, G.I, G.D);
    fprintf(coordslog, "%-9s\t%-10s\t%-10s\t%-10s\t%-10s\t%-10s\n", "time", "target", "curpos", "speed", "accel", "error");
    ramptype_t ramp = RAMP_AMOUNT;
    if(*G.ramptype == 'd' || *G.ramptype == 'D') ramp = RAMP_DUMB;
    else if(*G.ramptype == 't' || *G.ramptype == 'T') ramp = RAMP_TRAPEZIUM;
    else if(*G.ramptype == 's' || *G.ramptype == 'S') ramp = RAMP_S;
    else ERRX("Point \"d\" (dumb), \"s\" (s-type), or \"t\" (trapez) for ramp type");
    model = init_moving(ramp, &limits);
    if(!model) ERRX("Can't init moving model: check parameters");
    start_model(G.Tend);
    fclose(coordslog);
    return 0;
}
