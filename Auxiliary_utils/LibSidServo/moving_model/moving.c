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
#include <pthread.h>
#include <time.h>

#include "moving.h"
#include "moving_private.h"
#include "Dramp.h"
#include "Sramp.h"
#include "Tramp.h"

//static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static movemodel_t *model = NULL;
double coord_tolerance = COORD_TOLERANCE_DEFAULT;
double time_tick = TIME_TICK_DEFAULT;

// difference of time from first call, using nanoseconds
double nanot(){
    static struct timespec *start = NULL;
    struct timespec now;
    if(!start){
        start = MALLOC(struct timespec, 1);
        if(!start) return -1.;
        if(clock_gettime(CLOCK_REALTIME, start)) return -1.;
    }
    if(clock_gettime(CLOCK_REALTIME, &now)) return -1.;
    //DBG("was: %ld, now: %ld", start->tv_nsec, now.tv_nsec);
    double nd = ((double)now.tv_nsec - (double)start->tv_nsec) * 1e-9;
    double sd = (double)now.tv_sec - (double)start->tv_sec;
    return sd + nd;
}

static void* thread(void _U_ *u){
    if(!model) return NULL;
    DBG("START thread");
    double t = 0.;
    while(1){
        t = nanot();
        //pthread_mutex_lock(&mutex);
        movestate_t curstate = model->get_state(NULL);
        moveparam_t curmove;
        if(curstate == ST_MOVE){
            curstate = model->proc_move(&curmove, t);
        }
        //pthread_mutex_unlock(&mutex);
        while(nanot() - t < time_tick);
    }
    return NULL;
}

static void chkminmax(double *min, double *max){
    if(*min <= *max) return;
    double t = *min;
    *min = *max;
    *max = t;
}

movemodel_t *init_moving(ramptype_t type, limits_t *l){
    if(!l) return FALSE;
    pthread_t t;
    switch(type){
        case RAMP_DUMB:
            model = &dumb;
        break;
        case RAMP_TRAPEZIUM:
            model = &trapez;
        break;
        case RAMP_S:
            model = &s_shaped;
        break;
        default:
            return FALSE;
    }
    if(!model->init_limits) return NULL;
    moveparam_t *max = &l->max, *min = &l->min;
    if(min->speed < 0.) min->speed = -min->speed;
    if(max->speed < 0.) max->speed = -max->speed;
    if(min->accel < 0.) min->accel = -min->accel;
    if(max->accel < 0.) max->accel = -max->accel;
    chkminmax(&min->coord, &max->coord);
    chkminmax(&min->speed, &max->speed);
    chkminmax(&min->accel, &max->accel);
    if(!model->init_limits(l)) return NULL;
    if(pthread_create(&t, NULL, thread, NULL)) return NULL;
    pthread_detach(t);
    return model;
}

int move_to(moveparam_t *target){
    if(!target || !model) return FALSE;
    // only positive velocity
    if(target->speed < 0.) target->speed = -target->speed;
    // don't mind about acceleration - user cannot set it now
    return model->calculate(target, nanot());
}

int init_coordtol(double tolerance){
    if(tolerance < COORD_TOLERANCE_MIN || tolerance > COORD_TOLERANCE_MAX) return FALSE;
    coord_tolerance = tolerance;
    return TRUE;
}
int init_timetick(double tick){
    if(tick < TIME_TICK_MIN || tick > TIME_TICK_MAX) return FALSE;
    time_tick = tick;
    return TRUE;
}
