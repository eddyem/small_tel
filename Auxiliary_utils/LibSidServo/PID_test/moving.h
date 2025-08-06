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

// tolerance, time ticks
#define COORD_TOLERANCE_DEFAULT (0.01)
#define COORD_TOLERANCE_MIN     (0.0001)
#define COORD_TOLERANCE_MAX     (10.)
#define TIME_TICK_DEFAULT       (0.0001)
#define TIME_TICK_MIN           (1e-9)
#define TIME_TICK_MAX           (10.)

typedef enum{
    ST_STOP,            // stopped
    ST_MOVE,            // moving
    ST_AMOUNT
} movestate_t;

typedef struct{ // all values could be both as positive and negative
    double coord;
    double speed;
    double accel;
} moveparam_t;

typedef struct{
    moveparam_t min;
    moveparam_t max;
} limits_t;

typedef struct{
    int (*init_limits)(limits_t *lim);                      // init values of limits, jerk
    int (*calculate)(moveparam_t *target, double t);        // calculate stages of traectory beginning from t
    movestate_t (*proc_move)(moveparam_t *next, double t);  // calculate next model point for time t
    movestate_t (*get_state)(moveparam_t *cur);             // get current moving state
    void (*stop)(double t);                                 // stop by ramp
    void (*emergency_stop)(double t);                       // stop with highest acceleration
    double (*stoppedtime)();                                // time when moving will ends
} movemodel_t;

extern double coord_tolerance;

double nanot();
movemodel_t *init_moving(limits_t *l);
int init_coordtol(double tolerance);
int init_timetick(double tick);
int move_to(moveparam_t *target, double t);
