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
#include <stdlib.h>
#include <strings.h>
#include <usefull_macros.h>

#include "PID.h"

PIDController_t *pid_create(PIDpar_t *gain, size_t Iarrsz){
    if(!gain || Iarrsz < 3) return NULL;
    PIDController_t *pid = (PIDController_t*)calloc(1, sizeof(PIDController_t));
    pid->gain = *gain;
    pid->pidIarrSize = Iarrsz;
    pid->pidIarray = (double*)calloc(Iarrsz, sizeof(double));
    return pid;
}

void pid_clear(PIDController_t *pid){
    if(!pid) return;
    DBG("CLEAR PID PARAMETERS");
    bzero(pid->pidIarray, sizeof(double) * pid->pidIarrSize);
    pid->integral = 0.;
    pid->prev_error = 0.;
    pid->curIidx = 0;
}

void pid_delete(PIDController_t **pid){
    if(!pid || !*pid) return;
    if((*pid)->pidIarray) free((*pid)->pidIarray);
    free(*pid);
    *pid = NULL;
}

double pid_calculate(PIDController_t *pid, double error, double dt){
    // calculate flowing integral
    double oldi = pid->pidIarray[pid->curIidx], newi = error * dt;
    DBG("oldi/new: %g, %g", oldi, newi);
    pid->pidIarray[pid->curIidx++] = newi;
    if(pid->curIidx >= pid->pidIarrSize) pid->curIidx = 0;
    pid->integral += newi - oldi;
    double derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;
    double sum = pid->gain.P * error + pid->gain.I * pid->integral + pid->gain.D * derivative;
    DBG("P=%g, I=%g, D=%g; sum=%g", pid->gain.P * error, pid->gain.I * pid->integral, pid->gain.D * derivative, sum);
    return sum;
}
