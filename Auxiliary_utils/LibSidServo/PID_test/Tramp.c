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

// simplest trapezioidal ramp

#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <usefull_macros.h>

#include "Tramp.h"

#undef DBG
#define DBG(...)

static movestate_t state = ST_STOP;
static moveparam_t Min, Max; // `Min` acceleration not used!

typedef enum{
    STAGE_ACCEL,        // start from zero speed and accelerate to Max speed
    STAGE_MAXSPEED,     // go with target speed
    STAGE_DECEL,        // go from target speed to zero
    STAGE_STOPPED,      // stop
    STAGE_AMOUNT
} movingstage_t;

static movingstage_t movingstage = STAGE_STOPPED;
static double Times[STAGE_AMOUNT] = {0}; // time when each stage starts
static moveparam_t Params[STAGE_AMOUNT] = {0}; // starting parameters for each stage
static moveparam_t curparams = {0}; // current coordinate/speed/acceleration

static int initlims(limits_t *lim){
    if(!lim) return FALSE;
    Min = lim->min;
    Max = lim->max;
    return TRUE;
}

static void emstop(double _U_ t){
    curparams.accel = 0.;
    curparams.speed = 0.;
    bzero(Times, sizeof(Times));
    bzero(Params, sizeof(Params));
    state = ST_STOP;
    movingstage = STAGE_STOPPED;
}

static void stop(double t){
    if(state == ST_STOP || movingstage == STAGE_STOPPED) return;
    movingstage = STAGE_DECEL;
    state = ST_MOVE;
    Times[STAGE_DECEL] = t;
    Params[STAGE_DECEL].speed = curparams.speed;
    if(curparams.speed > 0.) Params[STAGE_DECEL].accel = -Max.accel;
    else Params[STAGE_DECEL].accel = Max.accel;
    Params[STAGE_DECEL].coord = curparams.coord;
    // speed: v=v2+a2(t-t2), v2 and a2 have different signs; t3: v3=0 -> t3=t2-v2/a2
    Times[STAGE_STOPPED] = t - curparams.speed / Params[STAGE_DECEL].accel;
    // coordinate: x=x2+v2(t-t2)+a2(t-t2)^2/2 -> x3=x2+v2(t3-t2)+a2(t3-t2)^2/2
    double dt = Times[STAGE_STOPPED] - t;
    Params[STAGE_STOPPED].coord = curparams.coord + curparams.speed * dt +
                                  Params[STAGE_DECEL].accel * dt * dt / 2.;
}

/**
 * @brief calc - moving calculation
 * @param x - using max speed (>0!!!) and coordinate
 * @param t - current time value
 * @return FALSE if can't move with given parameters
 */
static int calc(moveparam_t *x, double t){
    if(!x) return FALSE;
    if(x->coord < Min.coord || x->coord > Max.coord) return FALSE;
    if(x->speed < Min.speed || x->speed > Max.speed) return FALSE;
    double Dx = fabs(x->coord - curparams.coord); // full distance
    double sign = (x->coord > curparams.coord) ? 1. : -1.; // sign of target accelerations and speeds
    // we have two variants: with or without stage with constant speed
    double dt23 = x->speed / Max.accel; // time of deceleration stage for given speed
    double dx23 = x->speed * dt23 / 2.; // distance on dec stage (abs)
    DBG("Dx=%g, sign=%g, dt23=%g, dx23=%g", Dx, sign, dt23, dx23);
    double setspeed = x->speed; // new max speed (we can change it if need)
    double dt01, dx01; // we'll fill them depending on starting conditions
    Times[0] = t;
    Params[0].speed = curparams.speed;
    Params[0].coord = curparams.coord;

    double curspeed = fabs(curparams.speed);
    double dt0s = curspeed / Max.accel; // time of stopping phase
    double dx0s = curspeed * dt0s / 2.; // distance
    DBG("dt0s=%g, dx0s=%g", dt0s, dx0s);
    if(dx0s > Dx){
        WARNX("distance too short");
        return FALSE;
    }
    if(fabs(Dx - dx0s) < coord_tolerance){ // just stop and we'll be on target
        DBG("Distance good to just stop");
        stop(t);
        return TRUE;
    }
    if(curparams.speed * sign < 0. || state == ST_STOP){ // we should change speed sign
        // after stop we will have full profile
        double dxs3 = Dx - dx0s;
        double newspeed = sqrt(Max.accel * dxs3);
        if(newspeed < setspeed) setspeed = newspeed; // we can't reach user speed
        DBG("dxs3=%g, setspeed=%g", dxs3, setspeed);
        dt01 = fabs(sign*setspeed - curparams.speed) / Max.accel;
        Params[0].accel = sign * Max.accel;
        if(state == ST_STOP) dx01 = setspeed * dt01 / 2.;
        else dx01 = dt01 * (dt01 / 2. * Max.accel - curspeed);
        DBG("dx01=%g, dt01=%g", dx01, dt01);
    }else{ // increase or decrease speed without stopping phase
        dt01 = fabs(sign*setspeed - curparams.speed) / Max.accel;
        double a = sign * Max.accel;
        if(sign * curparams.speed < 0.){DBG("change direction"); a = -a;}
        else if(curspeed > setspeed){ DBG("lower speed @ this direction"); a = -a;}
        //double a = (curspeed > setspeed) ? -Max.accel : Max.accel;
        dx01 = curspeed * dt01 + a * dt01 * dt01 / 2.;
        DBG("dt01=%g, a=%g, dx01=%g", dt01, a, dx01);
        if(dx01 + dx23 > Dx){ // calculate max speed
            setspeed = sqrt(Max.accel * Dx - curspeed * curspeed / 2.);
            if(setspeed < curspeed){
                setspeed = curparams.speed;
                dt01 = 0.; dx01 = 0.;
                Params[0].accel = 0.;
            }else{
                Params[0].accel = a;
                dt01 = fabs(setspeed - curspeed) / Max.accel;
                dx01 = curspeed * dt01 + Max.accel * dt01 * dt01 / 2.;
            }
        }else Params[0].accel = a;
    }
    if(setspeed < Min.speed){
        WARNX("New speed should be too small");
        return FALSE;
    }
    moveparam_t *p = &Params[STAGE_MAXSPEED];
    p->accel = 0.; p->speed = sign * setspeed;
    p->coord = curparams.coord + dx01 * sign;
    Times[STAGE_MAXSPEED] = Times[0] + dt01;
    dt23 = setspeed / Max.accel;
    dx23 = setspeed * dt23 / 2.;
    // calculate dx12 and dt12
    double dx12 = Dx - dx01 - dx23;
    if(dx12 < -coord_tolerance){
        WARNX("Oops, WTF dx12=%g?", dx12);
        return FALSE;
    }
    double dt12 = dx12 / setspeed;
    p = &Params[STAGE_DECEL];
    p->accel = -sign * Max.accel;
    p->speed = sign * setspeed;
    p->coord = Params[STAGE_MAXSPEED].coord + sign * dx12;
    Times[STAGE_DECEL] = Times[STAGE_MAXSPEED] + dt12;
    p = &Params[STAGE_STOPPED];
    p->accel = 0.; p->speed = 0.; p->coord = x->coord;
    Times[STAGE_STOPPED] = Times[STAGE_DECEL] + dt23;
    for(int i = 0; i < 4; ++i)
        DBG("%d: t=%g, coord=%g, speed=%g, accel=%g", i,
            Times[i], Params[i].coord, Params[i].speed, Params[i].accel);
    state = ST_MOVE;
    movingstage = STAGE_ACCEL;
    return TRUE;
}

static movestate_t proc(moveparam_t *next, double t){
    if(state == ST_STOP) goto ret;
    for(movingstage_t s = STAGE_STOPPED; s >= 0; --s){
        if(Times[s] <= t){ // check time for current stage
            movingstage = s;
            break;
        }
    }
    if(movingstage == STAGE_STOPPED){
        curparams.coord = Params[STAGE_STOPPED].coord;
        emstop(t);
        goto ret;
    }
    // calculate current parameters
    double dt = t - Times[movingstage];
    double a = Params[movingstage].accel;
    double v0 = Params[movingstage].speed;
    double x0 = Params[movingstage].coord;
    curparams.accel = a;
    curparams.speed = v0 + a * dt;
    curparams.coord = x0 + v0 * dt + a * dt * dt / 2.;
ret:
    if(next) *next = curparams;
    return state;
}

static movestate_t getst(moveparam_t *cur){
    if(cur) *cur = curparams;
    return state;
}

static double gettstop(){
    return Times[STAGE_STOPPED];
}

movemodel_t trapez = {
    .init_limits = initlims,
    .stop = stop,
    .emergency_stop = emstop,
    .get_state = getst,
    .calculate = calc,
    .proc_move = proc,
    .stoppedtime = gettstop,
};
