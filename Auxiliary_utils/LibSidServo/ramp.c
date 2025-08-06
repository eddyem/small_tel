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
#include <strings.h>

#include "main.h"
#include "ramp.h"
/*
#ifdef EBUG
#undef DBG
#define DBG(...)
#endif
*/
static double coord_tolerance = COORD_TOLERANCE_DEFAULT;

static void emstop(movemodel_t *m, double _U_ t){
    FNAME();
    pthread_mutex_lock(&m->mutex);
    m->curparams.accel = 0.;
    m->curparams.speed = 0.;
    bzero(m->Times, sizeof(double) * STAGE_AMOUNT);
    bzero(m->Params, sizeof(moveparam_t) * STAGE_AMOUNT);
    m->state = ST_STOP;
    m->movingstage = STAGE_STOPPED;
    pthread_mutex_unlock(&m->mutex);
}

static void stop(movemodel_t *m, double t){
    FNAME();
    pthread_mutex_lock(&m->mutex);
    if(m->state == ST_STOP || m->movingstage == STAGE_STOPPED) goto ret;
    m->movingstage = STAGE_DECEL;
    m->state = ST_MOVE;
    m->Times[STAGE_DECEL] = t;
    m->Params[STAGE_DECEL].speed = m->curparams.speed;
    if(m->curparams.speed > 0.) m->Params[STAGE_DECEL].accel = -m->Max.accel;
    else m->Params[STAGE_DECEL].accel = m->Max.accel;
    m->Params[STAGE_DECEL].coord = m->curparams.coord;
    // speed: v=v2+a2(t-t2), v2 and a2 have different signs; t3: v3=0 -> t3=t2-v2/a2
    m->Times[STAGE_STOPPED] = t - m->curparams.speed / m->Params[STAGE_DECEL].accel;
    // coordinate: x=x2+v2(t-t2)+a2(t-t2)^2/2 -> x3=x2+v2(t3-t2)+a2(t3-t2)^2/2
    double dt = m->Times[STAGE_STOPPED] - t;
    m->Params[STAGE_STOPPED].coord = m->curparams.coord + m->curparams.speed * dt +
                                  m->Params[STAGE_DECEL].accel * dt * dt / 2.;
ret:
    pthread_mutex_unlock(&m->mutex);
}

// inner part of `calc`, could be called recoursively for hard case
static void unlockedcalc(movemodel_t *m, moveparam_t *x, double t){
    // signs
    double sign_a01 = 0., sign_a23 = 0., sign_vset = 0.; // accelerations on stages ACCEL and DECEL, speed on maxspeed stage
    // times
    double dt01 = 0., dt12 = 0., dt23 = 0.;
    // absolute speed at stage 23 (or in that point); absolute max acceleration
    double abs_vset = x->speed, abs_a = m->Max.accel;
    // absolute target movement
    double abs_Dx = fabs(x->coord - m->curparams.coord);
    if(m->state == ST_STOP && abs_Dx < coord_tolerance){
        DBG("Movement too small -> stay at place");
        return;
    }
    // signs of Dx and current speed
    double sign_Dx = (x->coord > m->curparams.coord) ? 1. : -1.;
    double v0 = m->curparams.speed;
    double sign_v0 = v0 < 0. ? -1 : 1., abs_v0 = fabs(v0);
    if(v0 == 0.) sign_v0 = 0.;
    // preliminary calculations (vset and dependent values could be changed)
    dt01 = fabs(abs_v0 - abs_vset) / abs_a;
    double abs_dx23 = abs_vset * abs_vset / 2. / abs_a;
    dt23 = abs_vset / abs_a;
    double abs_dx_stop = abs_v0 * abs_v0 / 2. / abs_a;
    if(sign_Dx * sign_v0 >= 0. && abs_dx_stop < abs_Dx){ // we shouldn't change speed direction
        if(fabs(abs_dx_stop - abs_Dx) <= coord_tolerance){ // simplest case: just stop
            //DBG("Simplest case: stop");
            dt01 = dt12 = 0.;
            sign_a23 = -sign_v0;
            dt23 = abs_v0 / abs_a;
        }else if(abs_vset < abs_v0){ // move with smaller speed than now: very simple case
            //DBG("Move with smaller speed");
            sign_a01 = sign_a23 = -sign_v0;
            sign_vset = sign_v0;
            double abs_dx01 = abs_v0 * dt01 - abs_a * dt01 * dt01 / 2.;
            double abs_dx12 = abs_Dx - abs_dx01 - abs_dx23;
            dt12 = abs_dx12 / abs_vset;
        }else{// move with larget speed
            //DBG("Move with larger speed");
            double abs_dx01 = abs_v0 * dt01 + abs_a * dt01 * dt01 / 2.;
            if(abs_Dx < abs_dx01 + abs_dx23){ // recalculate target speed and other
                abs_vset = sqrt(abs_a * abs_Dx + abs_v0 * abs_v0 / 2.);
                dt01 = fabs(abs_v0 - abs_vset) / abs_a;
                abs_dx01 = abs_v0 * dt01 + abs_a * dt01 * dt01 / 2.;
                dt23 = abs_vset / abs_a;
                abs_dx23 = abs_vset * abs_vset / 2. / abs_a;
                DBG("Can't reach target speed %g, take %g instead", x->speed, abs_vset);
            }
            sign_a01 = sign_Dx; // sign_v0 could be ZERO!!!
            sign_a23 = -sign_Dx;
            sign_vset = sign_Dx;
            double abs_dx12 = abs_Dx - abs_dx01 - abs_dx23;
            dt12 = abs_dx12 / abs_vset;
        }
    }else{
        // if we are here, we have the worst case: change speed direction
        DBG("Hardest case: change speed direction");
        // now we should calculate coordinate at which model stops and biuld new trapezium from that point
        double x0 = m->curparams.coord, v0 = m->curparams.speed;
        double xstop = x0 + sign_v0 * abs_dx_stop, tstop = t + abs_v0 / abs_a;
        m->state = ST_STOP;
        m->curparams.accel = 0.; m->curparams.coord = xstop; m->curparams.speed = 0.;
        unlockedcalc(m, x, tstop); // calculate new ramp
        // and change started conditions
        m->curparams.coord = x0; m->curparams.speed = v0;
        m->Times[STAGE_ACCEL] = t;
        m->Params[STAGE_ACCEL].coord = x0;
        m->Params[STAGE_ACCEL].speed = v0;
        DBG("NOW t[0]=%g, X[0]=%g, V[0]=%g", t, x0, v0);
        return;
    }
    m->state = ST_MOVE;
    m->movingstage = STAGE_ACCEL;
    // some knot parameters
    double a01 = sign_a01 * abs_a, a23 = sign_a23 * abs_a;
    double v1, v2, x0, x1, x2;
    v2 = v1 = sign_vset * abs_vset;
    x0 = m->curparams.coord;
    x1 = x0 + v0 * dt01 + a01 * dt01 * dt01 / 2.;
    x2 = x1 + v1 * dt12;
    // fill knot parameters
    moveparam_t *p = &m->Params[STAGE_ACCEL]; // 0-1 - change started speed
    p->accel = a01;
    p->speed = m->curparams.speed;
    p->coord = x0;
    m->Times[STAGE_ACCEL] = t;
    p = &m->Params[STAGE_MAXSPEED]; // 1-2 - constant speed
    p->accel = 0.;
    p->speed = v1;
    p->coord = x1;
    m->Times[STAGE_MAXSPEED] = m->Times[STAGE_ACCEL] + dt01;
    p = &m->Params[STAGE_DECEL]; // 2-3 - decrease speed
    p->accel = a23;
    p->speed = v2;
    p->coord = x2;
    m->Times[STAGE_DECEL] = m->Times[STAGE_MAXSPEED] + dt12;
    p = &m->Params[STAGE_STOPPED]; // 3 - stop at target
    p->accel = p->speed = 0.;
    p->coord = x->coord;
    m->Times[STAGE_STOPPED] = m->Times[STAGE_DECEL] + dt23;
}

/**
 * @brief calc - moving calculation
 * @param x - using max speed (>0!!!) and coordinate
 * @param t - current time value
 * @return FALSE if can't move with given parameters
 */
static int calc(movemodel_t *m, moveparam_t *x, double t) {
    //DBG("target coord/speed: %g/%g; current: %g/%g", x->coord, x->speed, m->curparams.coord, m->curparams.speed);
    if (!x || !m) return FALSE;
    pthread_mutex_lock(&m->mutex);
    int ret = FALSE;
    // Validate input parameters
    if(x->coord < m->Min.coord || x->coord > m->Max.coord){
        DBG("Wrong coordinate [%g, %g]", m->Min.coord, m->Max.coord);
        goto ret;
    }
    if(x->speed < m->Min.speed || x->speed > m->Max.speed){
        DBG("Wrong speed [%g, %g]", m->Min.speed, m->Max.speed);
        goto ret;
    }
    ret = TRUE; // now there's no chanses to make error
    unlockedcalc(m, x, t);
    // Debug output
    /*for(int i = 0; i < STAGE_AMOUNT; i++){
        DBG("Stage %d: t=%.6f, coord=%.6f, speed=%.6f, accel=%.6f",
            i, m->Times[i], m->Params[i].coord, m->Params[i].speed, m->Params[i].accel);
    }*/
ret:
    pthread_mutex_unlock(&m->mutex);
    return ret;
}

static movestate_t proc(movemodel_t *m, moveparam_t *next, double t){
    pthread_mutex_lock(&m->mutex);
    if(m->state == ST_STOP) goto ret;
    for(movingstage_t s = STAGE_STOPPED; s >= 0; --s){
        if(m->Times[s] <= t){ // check time for current stage
            m->movingstage = s;
            break;
        }
    }
    if(m->movingstage == STAGE_STOPPED){
        m->curparams.coord = m->Params[STAGE_STOPPED].coord;
        pthread_mutex_unlock(&m->mutex);
        DBG("REACHED STOPping stage @ t=%g", t);
        for(int s = STAGE_STOPPED; s >= 0; --s){
            DBG("T[%d]=%g, ", s, m->Times[s]);
        }
        fflush(stdout);
        emstop(m, t);
        goto ret;
    }
    // calculate current parameters
    double dt = t - m->Times[m->movingstage];
    double a = m->Params[m->movingstage].accel;
    double v0 = m->Params[m->movingstage].speed;
    double x0 = m->Params[m->movingstage].coord;
    m->curparams.accel = a;
    m->curparams.speed = v0 + a * dt;
    m->curparams.coord = x0 + v0 * dt + a * dt * dt / 2.;
ret:
    if(next) *next = m->curparams;
    movestate_t st = m->state;
    pthread_mutex_unlock(&m->mutex);
    return st;
}

static movestate_t getst(movemodel_t *m, moveparam_t *cur){
    pthread_mutex_lock(&m->mutex);
    if(cur) *cur = m->curparams;
    movestate_t st = m->state;
    pthread_mutex_unlock(&m->mutex);
    return st;
}

static double gettstop(movemodel_t *m){
    pthread_mutex_lock(&m->mutex);
    double r = m->Times[STAGE_STOPPED];
    pthread_mutex_unlock(&m->mutex);
    return r;
}

movemodel_t trapez = {
    .stop = stop,
    .emergency_stop = emstop,
    .get_state = getst,
    .calculate = calc,
    .proc_move = proc,
    .stoppedtime = gettstop,
};
