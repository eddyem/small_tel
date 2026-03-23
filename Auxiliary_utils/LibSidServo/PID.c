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

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "main.h"
#include "PID.h"
#include "serial.h"

typedef struct {
    PIDpar_t gain;      // PID gains
    double prev_error;  // Previous error
    double prev_tagpos; // previous target position
    double integral;    // Integral term
    double *pidIarray;  // array for Integral
    struct timespec prevT; // time of previous correction
    size_t pidIarrSize; // it's size
    size_t curIidx;     // and index of current element
} PIDController_t;

typedef struct{
    axis_status_t state;
    coordval_t position;
    coordval_t speed;
} axisdata_t;

static PIDController_t *pid_create(const PIDpar_t *gain, size_t Iarrsz){
    if(!gain || Iarrsz < 3) return NULL;
    PIDController_t *pid = (PIDController_t*)calloc(1, sizeof(PIDController_t));
    pid->gain = *gain;
    DBG("Created PID with P=%g, I=%g, D=%g\n", gain->P, gain->I, gain->D);
    pid->pidIarrSize = Iarrsz;
    pid->pidIarray = (double*)calloc(Iarrsz, sizeof(double));
    curtime(&pid->prevT);
    return pid;
}

// don't clear lastT!
static void pid_clear(PIDController_t *pid){
    if(!pid) return;
    DBG("CLEAR PID PARAMETERS");
    bzero(pid->pidIarray, sizeof(double) * pid->pidIarrSize);
    pid->integral = 0.;
    pid->prev_error = 0.;
    pid->curIidx = 0;
    curtime(&pid->prevT);
}
/*
static void pid_delete(PIDController_t **pid){
    if(!pid || !*pid) return;
    if((*pid)->pidIarray) free((*pid)->pidIarray);
    free(*pid);
    *pid = NULL;
}*/

// calculate new motor speed
static double pid_calculate(PIDController_t *pid, double axispos, const coordval_t *target){
    double dtpid = timediff(&target->t, &pid->prevT);
    if(dtpid < 0 || dtpid > Conf.PIDMaxDt){
        DBG("time diff too big: clear PID");
        pid_clear(pid);
        pid->prev_tagpos = target->val;
        return 0.;
    }
    double dt = timediff(&target->t, &pid->prevT);
    if(dt < FLT_EPSILON){
        DBG("Target time in past");
        return 0.;
    }
    pid->prevT = target->t;
    double error = target->val - axispos;
    double tagspeed = (target->val - pid->prev_tagpos) / dt;
    pid->prev_tagpos = target->val;
    // calculate flowing integral
    double oldi = pid->pidIarray[pid->curIidx], newi = error * dt;
    //DBG("oldi/new: %g, %g", oldi, newi);
    pid->pidIarray[pid->curIidx++] = newi;
    if(pid->curIidx >= pid->pidIarrSize) pid->curIidx = 0;
    pid->integral += newi - oldi;
    double derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;
    DBG("pid pars: P=%g, I=%g, D=%f", pid->gain.P, pid->gain.I, pid->gain.D);
    double sum = pid->gain.P * error + pid->gain.I * pid->integral + pid->gain.D * derivative + tagspeed;
    DBG("tagspeed=%g, P=%g, I=%g, D=%g; sum=%g", tagspeed, pid->gain.P * error,
        pid->gain.I * pid->integral, pid->gain.D * derivative, sum);
    return sum;
}

/**
 * @brief process - Process PID for given axis
 * @param tagpos - given coordinate of target position
 * @param endpoint - endpoint for this coordinate
 * @param pid - pid itself
 * @return calculated NEW SPEED or NAN for max speed
 */
static double getspeed(const coordval_t *tagpos, PIDController_t *pid, axisdata_t *axis){
    double dt = timediff(&tagpos->t, &axis->position.t);
    if(dt < 0 || dt > Conf.PIDMaxDt){
        DBG("target time: %ld, axis time: %ld - too big! (tag-ax=%g)", tagpos->t.tv_sec, axis->position.t.tv_sec, dt);
        return axis->speed.val; // data is too old or wrong
    }
    double error = tagpos->val - axis->position.val, fe = fabs(error);
    DBG("error: %g'', cur speed: %g (deg/s)", error * 180. * 3600. / M_PI, axis->speed.val*180./M_PI);
    switch(axis->state){
        case AXIS_SLEWING:
            if(fe < Conf.MaxFinePointingErr){
                axis->state = AXIS_POINTING;
                DBG("--> Pointing");
            }else{
                DBG("Slewing...");
                return NAN; // max speed for given axis
            }
            break;
        case AXIS_POINTING:
            if(fe < Conf.MaxFinePointingErr){
                axis->state = AXIS_GUIDING;
                DBG("--> Guiding");
            }else if(fe > Conf.MaxPointingErr){
                DBG("--> Slewing");
                axis->state = AXIS_SLEWING;
                return NAN;
            }
            break;
        case AXIS_GUIDING:
            if(fe > Conf.MaxFinePointingErr){
                DBG("--> Pointing");
                axis->state = AXIS_POINTING;
            }else if(fe < Conf.MaxGuidingErr){
                DBG("At target");
                // TODO: we can point somehow that we are at target or introduce new axis state
            }else DBG("Current abs error: %g", fe);
            break;
        case AXIS_GONNASTOP:
        case AXIS_STOPPED: // start pointing to target; will change speed next time
            DBG("AXIS STOPPED!!!! --> Slewing");
            axis->state = AXIS_SLEWING;
            return getspeed(tagpos, pid, axis);
        case AXIS_ERROR:
            DBG("Can't move from erroneous state");
            return 0.;
    }
    if(!pid){
        DBG("WTF? Where is a PID?");
        return axis->speed.val;
    }
    return pid_calculate(pid, axis->position.val, tagpos);
}

/**
 * @brief correct2 - recalculate PID and move telescope to new point with new speed
 * @param target - target position (for error calculations)
 * @param endpoint - stop point (some far enough point to stop in case of hang)
 * @return error code
 */
mcc_errcodes_t correct2(const coordval_pair_t *target){
    static PIDController_t *pidX = NULL, *pidY = NULL;
    if(!pidX){
        pidX = pid_create(&Conf.XPIDV, Conf.PIDCycleDt / Conf.PIDRefreshDt);
        if(!pidX) return MCC_E_FATAL;
    }
    if(!pidY){
        pidY = pid_create(&Conf.YPIDV, Conf.PIDCycleDt / Conf.PIDRefreshDt);
        if(!pidY) return MCC_E_FATAL;
    }
    mountdata_t m;
    coordpair_t tagspeed; // absolute value of speed
    double Xsign = 1., Ysign = 1.; // signs of speed (for target calculation)
    if(MCC_E_OK != Mount.getMountData(&m)) return MCC_E_FAILED;
    axisdata_t axis;
    DBG("state: %d/%d", m.Xstate, m.Ystate);
    axis.state = m.Xstate;
    axis.position = m.encXposition;
    axis.speed = m.encXspeed;
    tagspeed.X = getspeed(&target->X, pidX, &axis);
    if(isnan(tagspeed.X)){ // max speed
        if(target->X.val < axis.position.val) Xsign = -1.;
        tagspeed.X = Xlimits.max.speed;
    }else{
        if(tagspeed.X < 0.){ tagspeed.X = -tagspeed.X; Xsign = -1.; }
        if(tagspeed.X > Xlimits.max.speed) tagspeed.X = Xlimits.max.speed;
    }
    axis_status_t xstate = axis.state;
    axis.state = m.Ystate;
    axis.position = m.encYposition;
    axis.speed = m.encYspeed;
    tagspeed.Y = getspeed(&target->Y, pidY, &axis);
    if(isnan(tagspeed.Y)){ // max speed
        if(target->Y.val < axis.position.val) Ysign = -1.;
        tagspeed.Y = Ylimits.max.speed;
    }else{
        if(tagspeed.Y < 0.){ tagspeed.Y = -tagspeed.Y; Ysign = -1.; }
        if(tagspeed.Y > Ylimits.max.speed) tagspeed.Y = Ylimits.max.speed;
    }
    axis_status_t ystate = axis.state;
    if(m.Xstate != xstate || m.Ystate != ystate){
        DBG("State changed");
        setStat(xstate, ystate);
    }
    coordpair_t endpoint;
#if 0
    // allow at least PIDMaxDt moving with target speed
    double dv = fabs(tagspeed.X - m.encXspeed.val);
    double adder = dv/Xlimits.max.accel * (m.encXspeed.val + dv / 2.) // distanse with changing speed
                   + Conf.PIDMaxDt * tagspeed.X // PIDMaxDt const speed moving
                   + tagspeed.X * tagspeed.X / Xlimits.max.accel / 2.; // stopping
    endpoint.X = m.encXposition.val + Xsign * adder;
    dv = fabs(tagspeed.Y - m.encYspeed.val);
    adder = dv/Ylimits.max.accel * (m.encYspeed.val + dv / 2.)
            + Conf.PIDMaxDt * tagspeed.Y
            + tagspeed.Y * tagspeed.Y / Ylimits.max.accel / 2.;
    endpoint.Y = m.encYposition.val + Ysign * adder;
#endif
    // allow 10s moving but not more than 10deg and not less than 1deg
    double adder = fabs(tagspeed.X) * 10.;
    if(adder > 0.17453) adder = 0.17453;
    else if(adder < 0.017453) adder = 0.017453;
    endpoint.X = m.encXposition.val + Xsign * adder;
    adder = fabs(tagspeed.Y) * 10.;
    if(adder > 0.17453) adder = 0.17453;
    else if(adder < 0.017453) adder = 0.017453;
    endpoint.Y = m.encYposition.val + Ysign * adder;
    DBG("TAG speeds: %g/%g (deg/s); TAG pos: %g/%g (deg)", tagspeed.X/M_PI*180., tagspeed.Y/M_PI*180., endpoint.X/M_PI*180., endpoint.Y/M_PI*180.);
    return Mount.moveWspeed(&endpoint, &tagspeed);
}
