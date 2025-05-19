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
#include <usefull_macros.h>

#include "Dramp.h"

static movestate_t state = ST_STOP;
static moveparam_t target, Min, Max;
static double T0 = -1., Xlast0; // time when move starts, last stage starting coordinate
static moveparam_t curparams = {0}; // current coordinate/speed/acceleration
static double T1 = -1.; // time to switch into minimal speed for best coord tolerance

typedef enum{
    STAGE_NORMALSPEED,
    STAGE_MINSPEED,
    STAGE_STOPPED
} movingsage_t;

static movingsage_t movingstage = STAGE_STOPPED;

static int initlims(limits_t *lim){
    if(!lim) return FALSE;
    Min = lim->min;
    Max = lim->max;
    return TRUE;
}

static int calc(moveparam_t *x, double t){
    DBG("target: %g, tagspeed: %g (maxspeed: %g, minspeed: %g)", x->coord, x->speed, Max.speed, Min.speed);
    if(!x || t < 0.) return FALSE;
    if(x->speed > Max.speed || x->speed < Min.speed || x->coord < Min.coord || x->coord > Max.coord) return FALSE;
    double adist = fabs(x->coord - curparams.coord);
    DBG("want dist: %g", adist);
    if(adist < coord_tolerance) return TRUE; // we are at place
    if(adist < time_tick * Min.speed) return FALSE; // cannot reach with current parameters
    target = *x;
    if(x->speed * time_tick > adist) target.speed = adist / (10. * time_tick); // take at least 10 ticks to reach position
    if(target.speed < Min.speed) target.speed = Min.speed;
    DBG("Approximate tag speed: %g", target.speed);
    T0 = t;
    // calculate time to switch into minimal speed
    T1 = -1.; // no min speed phase
    if(target.speed > Min.speed){
        double dxpertick = target.speed * time_tick;
        DBG("dX per one tick: %g", dxpertick);
        double ticks_need = floor(adist / dxpertick);
        DBG("ticks need: %g", ticks_need);
        if(ticks_need < 1.) return FALSE; // cannot reach
        if(fabs(ticks_need * dxpertick - adist) > coord_tolerance){
            DBG("Need to calculate slow phase; can't reach for %g ticks at current speed", ticks_need);
            double dxpersmtick = Min.speed * time_tick;
            DBG("dX per smallest tick: %g", dxpersmtick);
            while(--ticks_need > 1.){
                double part = adist - ticks_need * dxpertick;
                double smticks = floor(part / dxpersmtick);
                double least = part - smticks * dxpersmtick;
                if(least < coord_tolerance) break;
            }
            DBG("now BIG ticks: %g, T1=T0+%g", ticks_need, ticks_need*time_tick);
            T1 = t + ticks_need * time_tick;
        }
    }
    state = ST_MOVE;
    Xlast0 = curparams.coord;
    if(target.speed > Min.speed) movingstage = STAGE_NORMALSPEED;
    else movingstage = STAGE_MINSPEED;
    if(x->coord < curparams.coord) target.speed *= -1.; // real speed
    curparams.speed = target.speed;
    return TRUE;
}

static void stop(double _U_ t){
    T0 = -1.;
    curparams.accel = 0.;
    curparams.speed = 0.;
    state = ST_STOP;
    movingstage = STAGE_STOPPED;
}

static movestate_t proc(moveparam_t *next, double t){
    if(T0 < 0.) return ST_STOP;
    curparams.coord = Xlast0 + (t - T0) * curparams.speed;
    //DBG("coord: %g (dT: %g, speed: %g)", curparams.coord, t-T0, curparams.speed);
    int ooops = FALSE; // oops - we are over target!
    if(curparams.speed < 0.){ if(curparams.coord < target.coord) ooops = TRUE;}
    else{ if(curparams.coord > target.coord) ooops = TRUE; }
    if(ooops){
        DBG("OOOps! We are (%g) over target (%g) -> stop", curparams.coord, target.coord);
        stop(t);
        if(next) *next = curparams;
        return state;
    }
    if(movingstage == STAGE_NORMALSPEED && T1 > 0.){ // check need of T1
        if(t >= T1){
            DBG("T1=%g, t=%g -->", T1, t);
            curparams.speed = (curparams.speed > 0.) ? Min.speed : -Min.speed;
            movingstage = STAGE_MINSPEED;
            Xlast0 = curparams.coord;
            T0 = T1;
            DBG("Go further with minimal speed");
        }
    }
    if(fabs(curparams.coord - target.coord) < coord_tolerance){ // we are at place
        DBG("OK, we are in place");
        stop(t);
    }
    if(next) *next = curparams;
    return state;
}

static movestate_t getst(moveparam_t *cur){
    if(cur) *cur = curparams;
    return state;
}

movemodel_t dumb = {
    .init_limits = initlims,
    .calculate = calc,
    .proc_move = proc,
    .stop = stop,
    .emergency_stop = stop,
    .get_state = getst,
};
