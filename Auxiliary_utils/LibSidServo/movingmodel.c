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
#include <string.h>
#include <pthread.h>

#include "main.h"
#include "movingmodel.h"
#include "ramp.h"

extern movemodel_t trapez;

static void chkminmax(double *min, double *max){
    if(*min <= *max) return;
    double t = *min;
    *min = *max;
    *max = t;
}

movemodel_t *model_init(limits_t *l){
    if(!l) return FALSE;
    movemodel_t *m = calloc(1, sizeof(movemodel_t));
    // we can't use memcpy or assign as Times/Params would be common for all
    *m = trapez;
    m->Times = calloc(STAGE_AMOUNT, sizeof(double));
    m->Params = calloc(STAGE_AMOUNT, sizeof(moveparam_t));
    moveparam_t *max = &l->max, *min = &l->min;
    if(min->speed < 0.) min->speed = -min->speed;
    if(max->speed < 0.) max->speed = -max->speed;
    if(min->accel < 0.) min->accel = -min->accel;
    if(max->accel < 0.) max->accel = -max->accel;
    chkminmax(&min->coord, &max->coord);
    chkminmax(&min->speed, &max->speed);
    chkminmax(&min->accel, &max->accel);
    m->Min = l->min;
    m->Max = l->max;
    m->movingstage = STAGE_STOPPED;
    m->state = ST_STOP;
    pthread_mutex_init(&m->mutex, NULL);
    DBG("model inited");
    return m;
}

int model_move2(movemodel_t *model, moveparam_t *target, double t){
    if(!target || !model) return FALSE;
    DBG("MOVE to %g (deg) at speed %g (deg/s)", target->coord/M_PI*180., target->speed/M_PI*180.);
    // only positive velocity
    if(target->speed < 0.) target->speed = -target->speed;
    if(fabs(target->speed) < model->Min.speed){
        DBG("STOP");
        model->stop(model, t);
        return TRUE;
    }
    // don't mind about acceleration - user cannot set it now
    return model->calculate(model, target, t);
}
