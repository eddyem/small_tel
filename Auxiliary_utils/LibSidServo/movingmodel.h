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

#pragma once
#include <pthread.h>

#include "sidservo.h"

// tolerance, time ticks
#define COORD_TOLERANCE_DEFAULT (1e-8)
#define COORD_TOLERANCE_MIN     (1e-12)
#define COORD_TOLERANCE_MAX     (10.)
#define TIME_TICK_DEFAULT       (0.0001)
#define TIME_TICK_MIN           (1e-9)
#define TIME_TICK_MAX           (10.)

typedef enum{
    ST_STOP,            // stopped
    ST_MOVE,            // moving
    ST_AMOUNT
} movestate_t;

typedef struct{
    double coord;
    double speed;
    double accel;
} moveparam_t;

typedef struct{
    moveparam_t min;
    moveparam_t max;
    double acceleration;
} limits_t;

typedef enum{
    STAGE_ACCEL,        // start from last speed and accelerate/decelerate to target speed
    STAGE_MAXSPEED,     // go with target speed
    STAGE_DECEL,        // go from target speed to zero
    STAGE_STOPPED,      // stop
    STAGE_AMOUNT
} movingstage_t;

typedef struct movemodel{
    moveparam_t Min;
    moveparam_t Max;
    movingstage_t movingstage;
    movestate_t state;
    double *Times;
    moveparam_t *Params;
    moveparam_t curparams;                    // init values of limits, jerk
    int (*calculate)(struct movemodel *m, moveparam_t *target, double t);        // calculate stages of traectory beginning from t
    movestate_t (*proc_move)(struct movemodel *m, moveparam_t *next, double t);  // calculate next model point for time t
    movestate_t (*get_state)(struct movemodel *m, moveparam_t *cur);             // get current moving state
    void (*stop)(struct movemodel *m, double t);                                 // stop by ramp
    void (*emergency_stop)(struct movemodel *m, double t);                       // stop with highest acceleration
    double (*stoppedtime)(struct movemodel *m);                                  // time when moving will ends
    pthread_mutex_t mutex;
} movemodel_t;

movemodel_t *model_init(limits_t *l);
int model_move2(movemodel_t *model, moveparam_t *target, double t);
