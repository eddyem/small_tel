/*
 * This file is part of the libsidservo project.
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

#include "main.h"
#include "PID.h"
#include "serial.h"

PIDController_t *pid_create(const PIDpar_t *gain, size_t Iarrsz){
    if(!gain || Iarrsz < 3) return NULL;
    PIDController_t *pid = (PIDController_t*)calloc(1, sizeof(PIDController_t));
    pid->gain = *gain;
    pid->pidIarrSize = Iarrsz;
    pid->pidIarray = (double*)calloc(Iarrsz, sizeof(double));
    return pid;
}

// don't clear lastT!
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
    //DBG("oldi/new: %g, %g", oldi, newi);
    pid->pidIarray[pid->curIidx++] = newi;
    if(pid->curIidx >= pid->pidIarrSize) pid->curIidx = 0;
    pid->integral += newi - oldi;
    double derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;
    double sum = pid->gain.P * error + pid->gain.I * pid->integral + pid->gain.D * derivative;
    DBG("P=%g, I=%g, D=%g; sum=%g", pid->gain.P * error, pid->gain.I * pid->integral, pid->gain.D * derivative, sum);
    return sum;
}

typedef struct{
    PIDController_t *PIDC;
    PIDController_t *PIDV;
} PIDpair_t;

typedef struct{
    axis_status_t state;
    coordval_t position;
    coordval_t speed;
} axisdata_t;
/**
 * @brief process - Process PID for given axis
 * @param tagpos - given coordinate of target position
 * @param endpoint - endpoint for this coordinate
 * @param pid - pid itself
 * @return calculated new speed or -1 for max speed
 */
static double getspeed(const coordval_t *tagpos, PIDpair_t *pidpair, axisdata_t *axis){
    if(tagpos->t < axis->position.t || tagpos->t - axis->position.t > MCC_PID_MAX_DT){
        DBG("target time: %g, axis time: %g - too big! (%g)", tagpos->t, axis->position.t, MCC_PID_MAX_DT);
        return axis->speed.val; // data is too old or wrong
    }
    double error = tagpos->val - axis->position.val, fe = fabs(error);
    PIDController_t *pid = NULL;
    switch(axis->state){
        case AXIS_SLEWING:
            if(fe < MCC_MAX_POINTING_ERR){
                axis->state = AXIS_POINTING;
                DBG("--> Pointing");
                pid = pidpair->PIDC;
            }else{
                DBG("Slewing...");
                return -1.; // max speed for given axis
            }
            break;
        case AXIS_POINTING:
            if(fe < MCC_MAX_GUIDING_ERR){
                axis->state = AXIS_GUIDING;
                DBG("--> Guiding");
                pid = pidpair->PIDV;
            }else if(fe > MCC_MAX_POINTING_ERR){
                DBG("--> Slewing");
                axis->state = AXIS_SLEWING;
                return -1.;
            } else pid = pidpair->PIDC;
            break;
        case AXIS_GUIDING:
            pid = pidpair->PIDV;
            if(fe > MCC_MAX_GUIDING_ERR){
                DBG("--> Pointing");
                axis->state = AXIS_POINTING;
                pid = pidpair->PIDC;
            }else if(fe < MCC_MAX_ATTARGET_ERR){
                DBG("At target");
                // TODO: we can point somehow that we are at target or introduce new axis state
            }else DBG("Current error: %g", fe);
            break;
        case AXIS_STOPPED: // start pointing to target; will change speed next time
            DBG("AXIS STOPPED!!!!");
            axis->state = AXIS_SLEWING;
            return -1.;
        case AXIS_ERROR:
            DBG("Can't move from erroneous state");
            return 0.;
    }
    if(!pid){
        DBG("WTF? Where is a PID?");
        return axis->speed.val;
    }
    if(tagpos->t < pid->prevT || tagpos->t - pid->prevT > MCC_PID_MAX_DT){
        DBG("time diff too big: clear PID");
        pid_clear(pid);
    }
    double dt = tagpos->t - pid->prevT;
    if(dt > MCC_PID_MAX_DT) dt = MCC_PID_CYCLE_TIME;
    pid->prevT = tagpos->t;
    //DBG("CALC PID (er=%g, dt=%g)", error, dt);
    double tagspeed = pid_calculate(pid, error, dt);
    if(axis->state == AXIS_GUIDING) return axis->speed.val + tagspeed; // velocity-based
    return tagspeed; // coordinate-based
}

/**
 * @brief correct2 - recalculate PID and move telescope to new point with new speed
 * @param target - target position (for error calculations)
 * @param endpoint - stop point (some far enough point to stop in case of hang)
 * @return error code
 */
mcc_errcodes_t correct2(const coordval_pair_t *target, const coordpair_t *endpoint){
    static PIDpair_t pidX = {0}, pidY = {0};
    if(!pidX.PIDC){
        pidX.PIDC = pid_create(&Conf.XPIDC, MCC_PID_CYCLE_TIME / MCC_PID_REFRESH_DT);
        if(!pidX.PIDC) return MCC_E_FATAL;
        pidX.PIDV = pid_create(&Conf.XPIDV, MCC_PID_CYCLE_TIME / MCC_PID_REFRESH_DT);
        if(!pidX.PIDV) return MCC_E_FATAL;
    }
    if(!pidY.PIDC){
        pidY.PIDC = pid_create(&Conf.YPIDC, MCC_PID_CYCLE_TIME / MCC_PID_REFRESH_DT);
        if(!pidY.PIDC) return MCC_E_FATAL;
        pidY.PIDV = pid_create(&Conf.YPIDV, MCC_PID_CYCLE_TIME / MCC_PID_REFRESH_DT);
        if(!pidY.PIDV) return MCC_E_FATAL;
    }
    mountdata_t m;
    coordpair_t tagspeed;
    if(MCC_E_OK != Mount.getMountData(&m)) return MCC_E_FAILED;
    axisdata_t axis;
    DBG("state: %d/%d", m.Xstate, m.Ystate);
    axis.state = m.Xstate;
    axis.position = m.encXposition;
    axis.speed = m.encXspeed;
    tagspeed.X = getspeed(&target->X, &pidX, &axis);
    if(tagspeed.X < 0. || tagspeed.X > MCC_MAX_X_SPEED) tagspeed.X = MCC_MAX_X_SPEED;
    axis_status_t xstate = axis.state;
    axis.state = m.Ystate;
    axis.position = m.encYposition;
    axis.speed = m.encYspeed;
    tagspeed.Y = getspeed(&target->Y, &pidY, &axis);
    if(tagspeed.Y < 0. || tagspeed.Y > MCC_MAX_Y_SPEED) tagspeed.Y = MCC_MAX_Y_SPEED;
    axis_status_t ystate = axis.state;
    if(m.Xstate != xstate || m.Ystate != ystate){
        DBG("State changed");
        setStat(xstate, ystate);
    }
    DBG("TAG speeds: %g/%g", tagspeed.X, tagspeed.Y);
    return Mount.moveWspeed(endpoint, &tagspeed);
}
